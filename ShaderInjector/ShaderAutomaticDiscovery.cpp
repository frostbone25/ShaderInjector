#include "ShaderAutomaticDiscovery.h"

#include <deque>
#include <mutex>
#include <unordered_set>
#include <wrl/client.h>

#include "DatabaseModifiedShaders.h"
#include "DatabaseShaderTargets.h"
#include "Hash.h"
#include "HookD3D12.h"
#include "HookD3D12PipelineUtils.h"
#include "ShaderDiscovery.h"
#include "ShaderInjectorGUI.h"

namespace ShaderAutomaticDiscovery
{
	namespace
	{
		constexpr size_t maximumQueuedShaders = 8192;

		struct ShaderKey
		{
			uint64_t hash = 0;
			ShaderTarget::ShaderType type = ShaderTarget::Unknown;

			bool operator==(const ShaderKey& other) const
			{
				return hash == other.hash && type == other.type;
			}
		};

		struct ShaderKeyHasher
		{
			size_t operator()(const ShaderKey& key) const
			{
				return static_cast<size_t>(key.hash ^ (static_cast<uint64_t>(key.type) << 57));
			}
		};

		enum class PipelineSource
		{
			Graphics,
			Stream,
		};

		struct QueuedShader
		{
			ShaderKey key;
			PipelineSource source = PipelineSource::Stream;
			int pipelineIndex = -1;
			Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
			std::vector<uint8_t> shaderBytecode;
		};

		std::mutex gQueueMutex;
		std::deque<QueuedShader> gQueuedShaders;
		std::unordered_set<ShaderKey, ShaderKeyHasher> gSubmittedShaders;
		std::unordered_set<ShaderKey, ShaderKeyHasher> gDirectMatchShaders;
		bool gCompatibleShaderTypes[static_cast<size_t>(ShaderTarget::Unknown) + 1]{};

		bool TargetContainsHash(const ShaderTarget::ShaderTargetDisk& target, uint64_t shaderHash)
		{
			if (Hash::ParseHashText(target.originalShaderBytecodeHash) == shaderHash)
				return true;

			for (const std::string& aliasHash : target.shaderBytecodeHashAliases)
			{
				if (Hash::ParseHashText(aliasHash) == shaderHash)
					return true;
			}
			return false;
		}

		void RebindGraphicsPipelinePointers(HookD3D12::GraphicsPipelineInfo& pipeline)
		{
			pipeline.originalDesc.VS = { pipeline.vsBytecode.empty() ? nullptr : pipeline.vsBytecode.data(), pipeline.vsBytecode.size() };
			pipeline.originalDesc.PS = { pipeline.psBytecode.empty() ? nullptr : pipeline.psBytecode.data(), pipeline.psBytecode.size() };
			pipeline.originalDesc.GS = { pipeline.gsBytecode.empty() ? nullptr : pipeline.gsBytecode.data(), pipeline.gsBytecode.size() };
			pipeline.originalDesc.HS = { pipeline.hsBytecode.empty() ? nullptr : pipeline.hsBytecode.data(), pipeline.hsBytecode.size() };
			pipeline.originalDesc.DS = { pipeline.dsBytecode.empty() ? nullptr : pipeline.dsBytecode.data(), pipeline.dsBytecode.size() };
			pipeline.originalDesc.InputLayout.pInputElementDescs = pipeline.inputElements.empty() ? nullptr : pipeline.inputElements.data();
			pipeline.originalDesc.InputLayout.NumElements = static_cast<UINT>(pipeline.inputElements.size());
			pipeline.originalDesc.StreamOutput.pSODeclaration = pipeline.soDeclarations.empty() ? nullptr : pipeline.soDeclarations.data();
			pipeline.originalDesc.StreamOutput.NumEntries = static_cast<UINT>(pipeline.soDeclarations.size());
			pipeline.originalDesc.StreamOutput.pBufferStrides = pipeline.soStrides.empty() ? nullptr : pipeline.soStrides.data();
			pipeline.originalDesc.StreamOutput.NumStrides = static_cast<UINT>(pipeline.soStrides.size());
		}

		bool Enqueue(
			PipelineSource source,
			ID3D12PipelineState* pipelineState,
			int pipelineIndex,
			ShaderTarget::ShaderType shaderType,
			uint64_t shaderHash,
			const std::vector<uint8_t>& shaderBytecode,
			bool force)
		{
			if (!pipelineState || shaderHash == 0 || shaderBytecode.empty())
				return false;

			const ShaderKey key{ shaderHash, shaderType };
			std::lock_guard<std::mutex> lock(gQueueMutex);
			const size_t shaderTypeIndex = static_cast<size_t>(shaderType);
			if (shaderTypeIndex >= static_cast<size_t>(ShaderTarget::Unknown) ||
				!gCompatibleShaderTypes[shaderTypeIndex])
			{
				return true;
			}
			const bool directMatch = gDirectMatchShaders.find(key) != gDirectMatchShaders.end();
			if (!force && !gSubmittedShaders.insert(key).second)
				return true;
			if (force)
			{
				gSubmittedShaders.insert(key);
				for (auto queuedIterator = gQueuedShaders.begin(); queuedIterator != gQueuedShaders.end();)
				{
					if (queuedIterator->key == key)
						queuedIterator = gQueuedShaders.erase(queuedIterator);
					else
						++queuedIterator;
				}
			}

			if (gQueuedShaders.size() >= maximumQueuedShaders)
			{
				if (!force)
					return false;
				gQueuedShaders.pop_back();
			}

			QueuedShader queued{};
			queued.key = key;
			queued.source = source;
			queued.pipelineIndex = pipelineIndex;
			queued.pipelineState = pipelineState;
			queued.shaderBytecode = shaderBytecode;
			if (force || directMatch)
				gQueuedShaders.push_front(std::move(queued));
			else
				gQueuedShaders.push_back(std::move(queued));
			return true;
		}

		bool CopyGraphicsPipeline(ID3D12PipelineState* pipelineState, HookD3D12::GraphicsPipelineInfo& outPipeline)
		{
			std::lock_guard<std::mutex> lock(HookD3D12::gPipelineMutex);
			for (const HookD3D12::GraphicsPipelineInfo& pipeline : HookD3D12::gGraphicsPipelines)
			{
				if (pipeline.pipelineState != pipelineState)
					continue;

				outPipeline = pipeline;
				if (outPipeline.originalDesc.pRootSignature)
					outPipeline.originalDesc.pRootSignature->AddRef();
				RebindGraphicsPipelinePointers(outPipeline);
				return true;
			}
			return false;
		}

		bool CopyStreamPipeline(ID3D12PipelineState* pipelineState, HookD3D12::PipelineStateInfo& outPipeline)
		{
			std::lock_guard<std::mutex> lock(HookD3D12::gPipelineMutex);
			for (const HookD3D12::PipelineStateInfo& pipeline : HookD3D12::gPipelineStates)
			{
				if (pipeline.pipelineState != pipelineState)
					continue;

				outPipeline = pipeline;
				if (outPipeline.rootSignature)
					outPipeline.rootSignature->AddRef();
				HookD3D12::RebindPipelineStateInfoPointerFields(outPipeline);
				return true;
			}
			return false;
		}

		template<typename PipelineType>
		bool CreateTargetForMatch(
			const char* sourceList,
			PipelineType& pipeline,
			const QueuedShader& queued,
			const std::string& modifiedShaderId)
		{
			if (!HookD3D12::CreateShaderTargetForPipeline(
				sourceList,
				queued.pipelineIndex,
				queued.key.type,
				queued.key.hash,
				queued.shaderBytecode.size(),
				queued.shaderBytecode.data(),
				pipeline,
				modifiedShaderId,
				false))
			{
				return false;
			}

			HookD3D12::RefreshLoadedShaderTargets();
			HookD3D12::MarkShaderTargetApplyDirty();
			ShaderInjectorGUI::WriteToRuntimeLogSuccess(
				"ShaderAutomaticDiscovery: created ShaderTarget for " + Hash::FormatHash(queued.key.hash) +
				" using " + modifiedShaderId);
			return true;
		}

		void ProcessQueuedShader(QueuedShader& queued)
		{
			DatabaseModifiedShaders::EnsureModifiedShadersLoaded();
			const std::vector<ModifiedShader::PackageDisk>& modifiedShaders = DatabaseModifiedShaders::GetModifiedShaders();
			const int modifiedShaderIndex = ShaderDiscovery::DiscoverEnabledModifiedShader(
				queued.key.hash,
				queued.key.type,
				queued.shaderBytecode,
				modifiedShaders);
			if (modifiedShaderIndex < 0 || modifiedShaderIndex >= static_cast<int>(modifiedShaders.size()))
				return;

			const std::string modifiedShaderId = modifiedShaders[modifiedShaderIndex].id;
			if (!HookD3D12::gLoadedShaderTargetsOnce)
				HookD3D12::RefreshLoadedShaderTargets();

			for (int targetIndex = 0; targetIndex < static_cast<int>(HookD3D12::gLoadedShaderTargets.size()); ++targetIndex)
			{
				const ShaderTarget::ShaderTargetDisk& existingTarget = HookD3D12::gLoadedShaderTargets[targetIndex];
				if (existingTarget.shaderType != queued.key.type || !TargetContainsHash(existingTarget, queued.key.hash))
					continue;

				if (existingTarget.modifiedShaderId != modifiedShaderId)
				{
					ShaderInjectorGUI::WriteToRuntimeLogError(
						"ShaderAutomaticDiscovery: shader " + Hash::FormatHash(queued.key.hash) +
						" already belongs to a target using a different ModifiedShader");
					return;
				}
				if (!existingTarget.enabled)
					return;

				const ModifiedShader::PackageDisk* existingPackage =
					DatabaseModifiedShaders::FindModifiedShaderById(modifiedShaderId);
				if (existingPackage && existingPackage->compiledBlob.empty())
				{
					if (!DatabaseModifiedShaders::CompileModifiedShader(modifiedShaderId) ||
						!HookD3D12::ReloadShaderTarget(targetIndex))
					{
						return;
					}
				}
				HookD3D12::MarkShaderTargetApplyDirty();
				return;
			}

			const ModifiedShader::PackageDisk* selectedPackage =
				DatabaseModifiedShaders::FindModifiedShaderById(modifiedShaderId);
			if (!selectedPackage)
				return;
			if (selectedPackage->compiledBlob.empty() &&
				!DatabaseModifiedShaders::CompileModifiedShader(modifiedShaderId))
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("ShaderAutomaticDiscovery: failed to compile " + modifiedShaderId);
				return;
			}

			if (queued.source == PipelineSource::Graphics)
			{
				HookD3D12::GraphicsPipelineInfo pipeline{};
				if (CopyGraphicsPipeline(queued.pipelineState.Get(), pipeline))
				{
					CreateTargetForMatch("Graphics", pipeline, queued, modifiedShaderId);
					if (pipeline.originalDesc.pRootSignature)
						pipeline.originalDesc.pRootSignature->Release();
				}
			}
			else
			{
				HookD3D12::PipelineStateInfo pipeline{};
				if (CopyStreamPipeline(queued.pipelineState.Get(), pipeline))
				{
					CreateTargetForMatch("Stream", pipeline, queued, modifiedShaderId);
					if (pipeline.rootSignature)
						pipeline.rootSignature->Release();
				}
			}
		}
	}

	void RefreshModifiedShaderIndex(const std::vector<ModifiedShader::PackageDisk>& modifiedShaders)
	{
		std::lock_guard<std::mutex> lock(gQueueMutex);
		gDirectMatchShaders.clear();
		for (bool& compatibleShaderType : gCompatibleShaderTypes)
			compatibleShaderType = false;

		for (const ModifiedShader::PackageDisk& modifiedShader : modifiedShaders)
		{
			if (!modifiedShader.enabled || modifiedShader.shaderType == ShaderTarget::Unknown)
				continue;

			const size_t shaderTypeIndex = static_cast<size_t>(modifiedShader.shaderType);
			if (shaderTypeIndex >= static_cast<size_t>(ShaderTarget::Unknown))
				continue;
			gCompatibleShaderTypes[shaderTypeIndex] = true;

			for (const ModifiedShader::TargetDisk& target : modifiedShader.targets)
			{
				for (const std::string& knownHash : target.knownShaderBytecodeHashes)
				{
					const uint64_t parsedHash = Hash::ParseHashText(knownHash);
					if (parsedHash != 0)
						gDirectMatchShaders.insert(ShaderKey{ parsedHash, modifiedShader.shaderType });
				}
			}
		}
	}

	void ProcessQueuedWork(size_t maximumJobs)
	{
		for (size_t processedJobs = 0; processedJobs < maximumJobs; ++processedJobs)
		{
			QueuedShader queued{};
			{
				std::lock_guard<std::mutex> lock(gQueueMutex);
				if (gQueuedShaders.empty())
					return;
				queued = std::move(gQueuedShaders.front());
				gQueuedShaders.pop_front();
			}

			ProcessQueuedShader(queued);
		}
	}

	void ProcessCapturedGraphicsPipeline(const HookD3D12::GraphicsPipelineInfo& pipeline)
	{
		Enqueue(PipelineSource::Graphics, pipeline.pipelineState, -1, ShaderTarget::VertexShader, pipeline.vsHash, pipeline.vsBytecode, false);
		Enqueue(PipelineSource::Graphics, pipeline.pipelineState, -1, ShaderTarget::PixelShader, pipeline.psHash, pipeline.psBytecode, false);
		Enqueue(PipelineSource::Graphics, pipeline.pipelineState, -1, ShaderTarget::GeometryShader, pipeline.gsHash, pipeline.gsBytecode, false);
		Enqueue(PipelineSource::Graphics, pipeline.pipelineState, -1, ShaderTarget::HullShader, pipeline.hsHash, pipeline.hsBytecode, false);
		Enqueue(PipelineSource::Graphics, pipeline.pipelineState, -1, ShaderTarget::DomainShader, pipeline.dsHash, pipeline.dsBytecode, false);
	}

	void ProcessCapturedStreamPipeline(const HookD3D12::PipelineStateInfo& pipeline)
	{
		Enqueue(PipelineSource::Stream, pipeline.pipelineState, -1, ShaderTarget::VertexShader, pipeline.vsHash, pipeline.vsBytecode, false);
		Enqueue(PipelineSource::Stream, pipeline.pipelineState, -1, ShaderTarget::PixelShader, pipeline.psHash, pipeline.psBytecode, false);
		Enqueue(PipelineSource::Stream, pipeline.pipelineState, -1, ShaderTarget::ComputeShader, pipeline.csHash, pipeline.csBytecode, false);
		Enqueue(PipelineSource::Stream, pipeline.pipelineState, -1, ShaderTarget::GeometryShader, pipeline.gsHash, pipeline.gsBytecode, false);
		Enqueue(PipelineSource::Stream, pipeline.pipelineState, -1, ShaderTarget::HullShader, pipeline.hsHash, pipeline.hsBytecode, false);
		Enqueue(PipelineSource::Stream, pipeline.pipelineState, -1, ShaderTarget::DomainShader, pipeline.dsHash, pipeline.dsBytecode, false);
	}

	bool ProcessCapturedShader(
		const HookD3D12::GraphicsPipelineInfo& pipeline,
		int pipelineIndex,
		ShaderTarget::ShaderType shaderType,
		uint64_t shaderHash,
		const std::vector<uint8_t>& shaderBytecode)
	{
		return Enqueue(PipelineSource::Graphics, pipeline.pipelineState, pipelineIndex, shaderType, shaderHash, shaderBytecode, true);
	}

	bool ProcessCapturedShader(
		const HookD3D12::PipelineStateInfo& pipeline,
		int pipelineIndex,
		ShaderTarget::ShaderType shaderType,
		uint64_t shaderHash,
		const std::vector<uint8_t>& shaderBytecode)
	{
		return Enqueue(PipelineSource::Stream, pipeline.pipelineState, pipelineIndex, shaderType, shaderHash, shaderBytecode, true);
	}
}
