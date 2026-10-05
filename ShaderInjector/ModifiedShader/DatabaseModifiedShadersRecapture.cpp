#include "DatabaseModifiedShaders.h"
#include "DatabaseModifiedShadersInternal.h"
#include "ModifiedShaderRecapture.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <unordered_set>
#include <utility>
#include <wrl/client.h>

#include "Hash/Hash.h"
#include "HookD3D12/HookD3D12.h"
#include "IO/ShaderInjectorIO.h"
#include "ShaderAnalysis/ShaderAnalyzer.h"
#include "ShaderDiscovery/ShaderAutomaticDiscovery.h"
#include "ShaderDiscovery/ShaderDiscovery.h"

namespace DatabaseModifiedShaders
{
	static bool TargetContainsShaderHash(const ShaderTarget::ShaderTargetDisk& target, uint64_t shaderHash)
	{
		if (Hash::ParseHashText(target.originalShaderBytecodeHash) == shaderHash)
			return true;
		return std::any_of(target.shaderBytecodeHashAliases.begin(), target.shaderBytecodeHashAliases.end(), [shaderHash](const std::string& alias)
		{
			return Hash::ParseHashText(alias) == shaderHash;
		});
	}

	static bool RefreshSavedStreamMetadata(ShaderTarget::ShaderTargetDisk& target)
	{
		if (target.pipelineStreamBlobPath.empty())
			return true;

		HookD3D12::PipelineStateInfo pipeline;
		if (!HookD3D12::LoadPersistedStreamTemplateFromReplacement(target, pipeline))
			return false;

		HookD3D12::FillStreamReplacementPortableStateFromBlob(target, pipeline);
		if (target.pipelineStreamMetadataPath.empty())
			target.pipelineStreamMetadataPath = ShaderInjectorIO::JoinPath(target.replacementDirectory, "PipelineStateStreamMetadata" + ShaderInjectorIO::extensionJSON);

		return ShaderTarget::WritePipelineStreamMetadataJson(target.pipelineStreamMetadataPath, HookD3D12::BuildPipelineStreamMetadata(pipeline));
	}

	static ShaderTarget::ShaderPipelineTemplateDisk CapturedStreamIdentity(const HookD3D12::PipelineStateInfo& pipeline)
	{
		ShaderTarget::ShaderPipelineTemplateDisk identity;
		HookD3D12::FillPipelineTemplateCommonState(identity, pipeline);
		std::vector<uint8_t> rootSignatureBlob;
		uint64_t rootSignatureHash = 0;
		if (HookD3D12::GetRootSignatureBlob(pipeline.rootSignature, rootSignatureBlob, rootSignatureHash))
			identity.rootSignatureHash = Hash::FormatHash(rootSignatureHash);
		return identity;
	}

	static bool MatchesStreamIdentity(const ShaderTarget::ShaderPipelineTemplateDisk& saved, const ShaderTarget::ShaderPipelineTemplateDisk& captured)
	{
		if (!saved.rootSignatureHash.empty() && saved.rootSignatureHash != captured.rootSignatureHash)
			return false;
		return HookD3D12::PipelineTemplateHasSameRebuildIdentity(saved, captured);
	}

	static bool RefreshStreamVariants(ShaderTarget::ShaderTargetDisk& target, const std::vector<std::pair<int, HookD3D12::PipelineStateInfo>>& liveCaptures)
	{
		bool succeeded = true;
		for (size_t variantIndex = 0; variantIndex < target.pipelineTemplates.size(); ++variantIndex)
		{
			ShaderTarget::ShaderPipelineTemplateDisk& variant = target.pipelineTemplates[variantIndex];
			bool refreshedFromLiveCapture = false;
			for (const auto& capture : liveCaptures)
			{
				const auto identity = CapturedStreamIdentity(capture.second);
				if (!MatchesStreamIdentity(variant, identity))
					continue;

				ShaderTarget::ShaderTargetDisk variantCapture;
				variantCapture.replacementDirectory = target.replacementDirectory;
				bool writeSucceeded = true;
				if (!HookD3D12::WriteStreamPipelineTemplateVariant(variantCapture, capture.second, capture.first, static_cast<int>(variantIndex), writeSucceeded) || !writeSucceeded)
					return false;

				auto refreshedVariant = std::move(variantCapture.pipelineTemplates.front());
				refreshedVariant.pipelineCachedBlobHashAliases = variant.pipelineCachedBlobHashAliases;
				if (refreshedVariant.pipelineCachedBlobHash.empty())
				{
					//a cache blob is driver-owned; keep the saved copy when this live PSO cannot provide a new one.
					refreshedVariant.pipelineCachedBlobHash = variant.pipelineCachedBlobHash;
					refreshedVariant.pipelineCachedBlobLength = variant.pipelineCachedBlobLength;
					refreshedVariant.pipelineCachedBlobPath = variant.pipelineCachedBlobPath;
				}
				if (!variant.pipelineCachedBlobHash.empty() && variant.pipelineCachedBlobHash != refreshedVariant.pipelineCachedBlobHash && std::find(refreshedVariant.pipelineCachedBlobHashAliases.begin(), refreshedVariant.pipelineCachedBlobHashAliases.end(), variant.pipelineCachedBlobHash) == refreshedVariant.pipelineCachedBlobHashAliases.end())
					refreshedVariant.pipelineCachedBlobHashAliases.push_back(variant.pipelineCachedBlobHash);
				variant = std::move(refreshedVariant);
				refreshedFromLiveCapture = true;
				break;
			}

			if (refreshedFromLiveCapture)
				continue;

			//a warmed-cache run may only have the saved original stream. reparse it without asking D3D12 to recreate the PSO.
			ShaderTarget::ShaderTargetDisk savedVariant = HookD3D12::ReplacementWithPipelineTemplate(target, variant);
			HookD3D12::PipelineStateInfo savedPipeline;
			if (!HookD3D12::LoadPersistedStreamTemplateFromReplacement(savedVariant, savedPipeline))
			{
				succeeded = false;
				continue;
			}
			HookD3D12::FillPipelineTemplateCommonState(variant, savedPipeline);
			if (!variant.pipelineStreamMetadataPath.empty())
				succeeded = ShaderTarget::WritePipelineStreamMetadataJson(variant.pipelineStreamMetadataPath, HookD3D12::BuildPipelineStreamMetadata(savedPipeline)) && succeeded;
		}

		//keep each fixed-function variant: local light volumes, for example, can share bytecode while using different depth/cull states.
		for (const auto& capture : liveCaptures)
		{
			const uint64_t hash = HookD3D12::StreamShaderHashForType(capture.second, target.shaderType);
			if (!TargetContainsShaderHash(target, hash))
				continue;

			const auto identity = CapturedStreamIdentity(capture.second);
			if (MatchesStreamIdentity(HookD3D12::TopLevelPipelineTemplate(target), identity) && target.rootSignatureHash == identity.rootSignatureHash)
				continue;
			const bool alreadyCaptured = std::any_of(target.pipelineTemplates.begin(), target.pipelineTemplates.end(), [&identity](const auto& variant)
			{
				return MatchesStreamIdentity(variant, identity);
			});
			if (!alreadyCaptured)
				succeeded = HookD3D12::WriteStreamPipelineTemplateVariant(target, capture.second, capture.first, static_cast<int>(target.pipelineTemplates.size()), succeeded) && succeeded;
		}
		return succeeded;
	}

	bool RecaptureModifiedShader(const std::string& modifiedShaderId, std::string& outMessage, uint64_t selectedShaderHash)
	{
		ModifiedShader::ModifiedShaderPackageDisk* loadedPackage = Detail::FindMutableModifiedShaderById(modifiedShaderId);
		if (!loadedPackage)
		{
			outMessage = "Cannot recapture: Modified Shader package was not found.";
			return false;
		}
		ModifiedShader::ModifiedShaderPackageDisk package = *loadedPackage;
		if (!HookD3D12::gLoadedShaderTargetsOnce)
			HookD3D12::RefreshLoadedShaderTargets();

		std::vector<ShaderTarget::ShaderTargetDisk> linkedTargets;
		std::unordered_set<uint64_t> knownHashes;
		for (const auto& fingerprint : package.targets)
			for (const std::string& hash : fingerprint.knownShaderBytecodeHashes)
				knownHashes.insert(Hash::ParseHashText(hash));

		for (const auto& target : HookD3D12::gLoadedShaderTargets)
		{
			if (selectedShaderHash && target.shaderType == package.shaderType && TargetContainsShaderHash(target, selectedShaderHash) && target.modifiedShaderId != modifiedShaderId)
			{
				outMessage = "Cannot recapture: the selected shader already belongs to another Modified Shader package.";
				return false;
			}
			if (target.modifiedShaderId != modifiedShaderId || target.shaderType != package.shaderType)
				continue;
			linkedTargets.push_back(target);
			knownHashes.insert(Hash::ParseHashText(target.originalShaderBytecodeHash));
			for (const auto& alias : target.shaderBytecodeHashAliases)
				knownHashes.insert(Hash::ParseHashText(alias));
		}
		knownHashes.erase(0);
		if (selectedShaderHash)
			knownHashes.insert(selectedShaderHash);

		std::vector<std::pair<int, HookD3D12::PipelineStateInfo>> streamCaptures;
		std::vector<std::pair<int, HookD3D12::GraphicsPipelineInfo>> graphicsCaptures;
		std::vector<Microsoft::WRL::ComPtr<ID3D12PipelineState>> retainedPipelines;
		std::vector<Microsoft::WRL::ComPtr<ID3D12RootSignature>> retainedRootSignatures;
		{
			//copy only matching original PSOs while holding the database lock; reflection and disk writes happen after releasing it.
			std::lock_guard<std::mutex> captureLock(HookD3D12::gPipelineMutex);
			for (size_t index = 0; index < HookD3D12::gPipelineStates.size(); ++index)
			{
				const auto& pipeline = HookD3D12::gPipelineStates[index];
				if (!knownHashes.count(HookD3D12::StreamShaderHashForType(pipeline, package.shaderType)))
					continue;
				streamCaptures.emplace_back(static_cast<int>(index), pipeline);
				retainedPipelines.emplace_back(pipeline.pipelineState);
				retainedRootSignatures.emplace_back(pipeline.rootSignature);
			}
			for (size_t index = 0; index < HookD3D12::gGraphicsPipelines.size(); ++index)
			{
				const auto& pipeline = HookD3D12::gGraphicsPipelines[index];
				if (!knownHashes.count(HookD3D12::GraphicsShaderHashForType(pipeline, package.shaderType)))
					continue;
				graphicsCaptures.emplace_back(static_cast<int>(index), pipeline);
				retainedPipelines.emplace_back(pipeline.pipelineState);
				retainedRootSignatures.emplace_back(pipeline.originalDescription.pRootSignature);
			}
		}

		std::map<uint64_t, std::vector<uint8_t>> originalBytecodes;
		std::unordered_set<uint64_t> liveHashes;
		for (auto& capture : streamCaptures)
		{
			HookD3D12::RebindPipelineStateInfoPointerFields(capture.second);
			const auto& bytecode = HookD3D12::StreamShaderBytecode(capture.second, package.shaderType);
			const uint64_t hash = Hash::HashMemory(bytecode.data(), bytecode.size());
			if (!bytecode.empty() && knownHashes.count(hash))
			{
				originalBytecodes.emplace(hash, bytecode);
				liveHashes.insert(hash);
			}
		}
		for (auto& capture : graphicsCaptures)
		{
			//semantic strings are owned by the snapshot, so point each descriptor back into that snapshot before serializing it.
			for (size_t index = 0; index < capture.second.inputElements.size(); ++index)
				capture.second.inputElements[index].SemanticName = capture.second.inputElementSemanticNames[index].c_str();
			for (size_t index = 0; index < capture.second.streamOutputDeclarations.size(); ++index)
				capture.second.streamOutputDeclarations[index].SemanticName = capture.second.streamOutputSemanticNames[index].c_str();
			const auto& bytecode = HookD3D12::GraphicsShaderBytecode(capture.second, package.shaderType);
			const uint64_t hash = Hash::HashMemory(bytecode.data(), bytecode.size());
			if (!bytecode.empty() && knownHashes.count(hash))
			{
				originalBytecodes.emplace(hash, bytecode);
				liveHashes.insert(hash);
			}
		}

		for (const auto& target : linkedTargets)
		{
			const uint64_t expectedHash = Hash::ParseHashText(target.originalShaderBytecodeHash);
			if (originalBytecodes.count(expectedHash))
				continue;
			std::vector<uint8_t> bytecode;
			if (ShaderInjectorIO::LoadDXILBlobFromDisk(target.originalShaderBlobPath, bytecode) && !bytecode.empty() && Hash::HashMemory(bytecode.data(), bytecode.size()) == expectedHash)
				originalBytecodes.emplace(expectedHash, std::move(bytecode));
		}

		if (originalBytecodes.empty() || (selectedShaderHash && !liveHashes.count(selectedShaderHash)))
		{
			outMessage = "Cannot recapture: no matching original shader bytecode is available. Capture the shader in the pipeline list first, or restore its Shader Target original bytecode files.";
			return false;
		}

		std::map<uint64_t, ShaderAnalysis::ShaderAnalysisDisk> analyses;
		for (const auto& original : originalBytecodes)
		{
			ShaderAnalysis::ShaderAnalysisDisk analysis;
			if (!ShaderAnalyzer::Analyze(original.second.data(), original.second.size(), analysis))
			{
				outMessage = "Recapture failed while analyzing original shader " + Hash::FormatHash(original.first) + ": " + analysis.error;
				return false;
			}
			if (!ModifiedShaderRecapture::UpdateFingerprint(package, original.first, original.second.size(), analysis))
			{
				outMessage = "Recapture refused original shader " + Hash::FormatHash(original.first) + ": its reflected shader stage does not match the Modified Shader package.";
				return false;
			}
			analyses.emplace(original.first, std::move(analysis));
		}

		size_t refreshedTargetCount = 0;
		bool targetWritesSucceeded = true;
		for (auto& target : linkedTargets)
		{
			HookD3D12::BackfillReplacementPortableMetadataFromSidecars(target);
			const uint64_t hash = Hash::ParseHashText(target.originalShaderBytecodeHash);
			const auto analysis = analyses.find(hash);
			if (analysis == analyses.end())
			{
				targetWritesSucceeded = false;
				ShaderInjectorIO::WriteToLogFileWarning("DatabaseModifiedShaders->RecaptureModifiedShader: original bytecode unavailable for " + target.name);
				continue;
			}

			target.originalShaderAnalysis = analysis->second;
			target.originalShaderBytecodeLength = std::to_string(originalBytecodes.at(hash).size());
			target.schemaVersion = ShaderTarget::ShaderTargetDisk{}.schemaVersion;
			bool recapturedLivePipeline = false;
			bool wroteTarget = true;
			for (auto& capture : streamCaptures)
			{
				if (HookD3D12::StreamShaderHashForType(capture.second, package.shaderType) != hash)
					continue;
				const auto identity = CapturedStreamIdentity(capture.second);
				if (target.sourceList == "Graphics")
					continue;
				if (!target.pipelineStreamBlobPath.empty() && !target.pipelineFixedFunctionStateHash.empty() && !MatchesStreamIdentity(HookD3D12::TopLevelPipelineTemplate(target), identity))
					continue;
				if (!target.rootSignatureHash.empty() && target.rootSignatureHash != identity.rootSignatureHash)
					continue;
				wroteTarget = HookD3D12::RecaptureShaderTargetForPipeline(target, capture.first, capture.second, analysis->second);
				recapturedLivePipeline = true;
				break;
			}
			if (!recapturedLivePipeline)
			{
				for (auto& capture : graphicsCaptures)
				{
					if (HookD3D12::GraphicsShaderHashForType(capture.second, package.shaderType) != hash)
						continue;
					ShaderTarget::ShaderTargetDisk capturedState;
					HookD3D12::FillCommonReplacementHashes(capturedState, capture.second.vertexShaderHash, capture.second.pixelShaderHash, 0, capture.second.geometryShaderHash, capture.second.hullShaderHash, capture.second.domainShaderHash);
					HookD3D12::FillGraphicsReplacementPortableState(capturedState, capture.second);
					std::vector<uint8_t> rootSignatureBlob;
					uint64_t rootSignatureHash = 0;
					if (HookD3D12::GetRootSignatureBlob(capture.second.originalDescription.pRootSignature, rootSignatureBlob, rootSignatureHash))
						capturedState.rootSignatureHash = Hash::FormatHash(rootSignatureHash);
					if (target.sourceList != "Graphics" || !ModifiedShaderRecapture::MatchesGraphicsPipelineState(target, capturedState))
						continue;
					wroteTarget = HookD3D12::RecaptureShaderTargetForPipeline(target, capture.first, capture.second, analysis->second);
					recapturedLivePipeline = true;
					break;
				}
			}
			if (!recapturedLivePipeline)
				wroteTarget = RefreshSavedStreamMetadata(target);
			wroteTarget = RefreshStreamVariants(target, streamCaptures) && wroteTarget;
			if (wroteTarget)
				wroteTarget = ShaderTarget::WriteShaderTargetJson(target);
			if (wroteTarget)
				++refreshedTargetCount;
			targetWritesSucceeded = wroteTarget && targetWritesSucceeded;
		}

		if (!ModifiedShader::WriteJson(package))
		{
			outMessage = "Recapture failed to write the Modified Shader fingerprint: " + package.jsonPath;
			return false;
		}
		//do not use RefreshModifiedShaders here: its interface-repair path can compile and overwrite the user's replacement blob.
		package.compiledShaderInterfaceCompatible = Detail::ShaderInterfaceMatchesAnyPackageTarget(package, package.compiledShaderAnalysis);
		if (!package.compiledBlob.empty() && !package.compiledShaderInterfaceCompatible)
			ShaderInjectorIO::WriteToLogFileWarning("DatabaseModifiedShaders->RecaptureModifiedShader: the preserved compiled blob is incompatible with the refreshed fingerprint for " + modifiedShaderId + "; recompile explicitly to change replacement bytecode");
		*loadedPackage = std::move(package);
		ShaderDiscovery::ResetRuntimeCache();
		ShaderAutomaticDiscovery::RefreshModifiedShaderIndex(GetModifiedShaders());
		HookD3D12::RefreshLoadedShaderTargets();

		//restore missing targets too, using the already compiled replacement exactly as it is.
		for (uint64_t capturedHash : liveHashes)
		{
			const bool alreadyLinked = std::any_of(linkedTargets.begin(), linkedTargets.end(), [capturedHash](const auto& target)
			{
				return TargetContainsShaderHash(target, capturedHash);
			});
			if (alreadyLinked)
				continue;
			const bool belongsToOtherPackage = std::any_of(HookD3D12::gLoadedShaderTargets.begin(), HookD3D12::gLoadedShaderTargets.end(), [&](const auto& target)
			{
				return target.shaderType == loadedPackage->shaderType && target.modifiedShaderId != modifiedShaderId && TargetContainsShaderHash(target, capturedHash);
			});
			if (belongsToOtherPackage)
			{
				targetWritesSucceeded = false;
				ShaderInjectorIO::WriteToLogFileWarning("DatabaseModifiedShaders->RecaptureModifiedShader: shader " + Hash::FormatHash(capturedHash) + " already belongs to another package; its target was preserved");
				continue;
			}
			bool createdTarget = false;
			for (auto& capture : streamCaptures)
			{
				if (HookD3D12::StreamShaderHashForType(capture.second, loadedPackage->shaderType) != capturedHash)
					continue;
				const auto& bytecode = originalBytecodes.at(capturedHash);
				createdTarget = HookD3D12::CreateShaderTargetForPipeline("Stream", capture.first, loadedPackage->shaderType, capturedHash, bytecode.size(), bytecode.data(), capture.second, modifiedShaderId, false, &analyses.at(capturedHash));
				break;
			}
			if (!createdTarget)
			{
				for (auto& capture : graphicsCaptures)
				{
					if (HookD3D12::GraphicsShaderHashForType(capture.second, loadedPackage->shaderType) != capturedHash)
						continue;
					const auto& bytecode = originalBytecodes.at(capturedHash);
					createdTarget = HookD3D12::CreateShaderTargetForPipeline("Graphics", capture.first, loadedPackage->shaderType, capturedHash, bytecode.size(), bytecode.data(), capture.second, modifiedShaderId, false, &analyses.at(capturedHash));
					break;
				}
			}
			targetWritesSucceeded = createdTarget && targetWritesSucceeded;
			if (createdTarget)
			{
				for (auto& target : HookD3D12::gLoadedShaderTargets)
				{
					if (target.modifiedShaderId != modifiedShaderId || Hash::ParseHashText(target.originalShaderBytecodeHash) != capturedHash)
						continue;
					const bool variantsRefreshed = RefreshStreamVariants(target, streamCaptures) && ShaderTarget::WriteShaderTargetJson(target);
					targetWritesSucceeded = variantsRefreshed && targetWritesSucceeded;
					break;
				}
				++refreshedTargetCount;
			}
		}

		outMessage = "Recaptured Modified Shader " + modifiedShaderId + ": fingerprints=" + std::to_string(analyses.size()) + " liveShaders=" + std::to_string(liveHashes.size()) + " savedOriginals=" + std::to_string(analyses.size() - liveHashes.size()) + " shaderTargets=" + std::to_string(refreshedTargetCount) + ". HLSL and compiled bytecode were preserved.";
		if (!targetWritesSucceeded)
			outMessage += " Some Shader Target data could not be refreshed; check the log for missing original files.";
		return targetWritesSucceeded;
	}
}
