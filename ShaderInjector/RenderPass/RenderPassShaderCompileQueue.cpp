#include "RenderPass/RenderPassShaderCompileQueue.h"

#include <atomic>
#include <exception>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "GUI/ShaderInjectorGUI.h"
#include "IO/ShaderInjectorIO.h"
#include "ModifiedShader/DatabaseModifiedShaders.h"
#include "RenderPass/RenderPassShaders.h"
#include "StringHelper.h"

namespace DatabaseRenderPasses
{
	namespace
	{
		struct CompletedRenderPassCompile
		{
			std::string id;
			std::string fingerprint;
			std::string packageDirectory;
			std::string jsonPath;
			uint64_t generation = 0;
			uint64_t serial = 0;
			RenderPass::RenderPassDisk snapshot;
			std::vector<std::string> compilerArguments;
			std::string error;
			bool succeeded = false;
		};

		struct PendingRenderPassCompile
		{
			uint64_t serial = 0;
			uint64_t generation = 0;
			std::vector<std::pair<std::vector<ModifiedShader::ShaderDefine>, uint64_t>> variants;
		};

		std::mutex gRenderPassCompileMutex;
		std::vector<CompletedRenderPassCompile> gCompletedRenderPassCompiles;
		std::unordered_map<std::string, PendingRenderPassCompile> gPendingRenderPassCompiles;
		RenderPassCompileProgress gRenderPassCompileProgress;
		std::atomic<uint64_t> gNextRenderPassCompileSerial = 1;
		uint64_t gRenderPassRefreshGeneration = 0;

		std::string ConfigurationFingerprint(const RenderPass::RenderPassDisk& renderPass)
		{
			return nlohmann::ordered_json(renderPass).dump();
		}

		void CountVariantCompletions(const std::string& renderPassId, uint64_t serial)
		{
			RenderPass::RenderPassDisk* livePass = FindRenderPassById(renderPassId);
			std::lock_guard<std::mutex> lock(gRenderPassCompileMutex);
			auto pendingIt = gPendingRenderPassCompiles.find(renderPassId);
			if (pendingIt == gPendingRenderPassCompiles.end() || pendingIt->second.serial != serial || pendingIt->second.variants.empty())
				return;
			PendingRenderPassCompile& pending = pendingIt->second;
			if (!livePass || pending.generation != gRenderPassRefreshGeneration)
			{
				const int discardedCount = static_cast<int>(pending.variants.size());
				gRenderPassCompileProgress.discarded += discardedCount;
				gRenderPassCompileProgress.completed += discardedCount;
				gRenderPassCompileProgress.queued -= discardedCount;
				gPendingRenderPassCompiles.erase(pendingIt);
				return;
			}

			for (auto variantIt = pending.variants.begin(); variantIt != pending.variants.end();)
			{
				ModifiedShader::ModifiedShaderCompiledVariant* variant = nullptr;
				for (ModifiedShader::ModifiedShaderCompiledVariant& candidate : livePass->fragmentShaderVariants)
				{
					if (candidate.defines == variantIt->first)
					{
						variant = &candidate;
						break;
					}
				}

				if (!variant)
				{
					++gRenderPassCompileProgress.discarded;
					++gRenderPassCompileProgress.completed;
					--gRenderPassCompileProgress.queued;
					variantIt = pending.variants.erase(variantIt);
					continue;
				}

				if (variant->pendingCompileSerial != 0 && variant->pendingCompileSerial != variantIt->second)
				{
					++gRenderPassCompileProgress.discarded;
					++gRenderPassCompileProgress.completed;
					--gRenderPassCompileProgress.queued;
					variantIt = pending.variants.erase(variantIt);
					continue;
				}

				if (variant->pendingCompileSerial != 0)
				{
					++variantIt;
					continue;
				}

				if (variant->compileFailed)
					++gRenderPassCompileProgress.failed;
				++gRenderPassCompileProgress.completed;
				--gRenderPassCompileProgress.queued;
				variantIt = pending.variants.erase(variantIt);
			}

			if (pending.variants.empty())
				gPendingRenderPassCompiles.erase(renderPassId);
		}
	}

	bool QueueRenderPassShaderCompile(const std::string& renderPassId, std::string& outError)
	{
		outError.clear();
		EnsureRenderPassesLoaded();

		RenderPass::RenderPassDisk* livePass = FindRenderPassById(renderPassId);
		if (!livePass)
		{
			outError = "Render Pass was not found.";
			return false;
		}

		if (gPendingRenderPassCompiles.find(renderPassId) != gPendingRenderPassCompiles.end())
		{
			outError = "Render Pass shader compilation is already queued.";
			return false;
		}

		RenderPass::ResolveShaderPaths(*livePass);
		if (!RenderPass::HasShaderTemplate(*livePass))
		{
			outError = "Render Pass shader source files are missing.";
			return false;
		}

		RenderPass::RenderPassDisk snapshot = *livePass;
		const std::string fingerprint = ConfigurationFingerprint(*livePass);
		const std::string packageDirectory = livePass->packageDirectory;
		const std::string jsonPath = livePass->jsonPath;
		const uint64_t serial = gNextRenderPassCompileSerial.fetch_add(1);
		const std::string stagingDirectory = ShaderInjectorIO::JoinPath(ShaderInjectorIO::GetInternalDirectory(), "RenderPassCompiles");
		ShaderInjectorIO::DirectoryCreate(stagingDirectory);

		const std::string serialText = std::to_string(serial);
		const bool compileVertexShader = !snapshot.vertexShaderSourcePath.empty();
		snapshot.vertexShaderCompiledBlobPath.clear();
		if (compileVertexShader)
			snapshot.vertexShaderCompiledBlobPath = ShaderInjectorIO::JoinPath(stagingDirectory, serialText + "_vertex.blob");
		snapshot.fragmentShaderCompiledBlobPath = ShaderInjectorIO::JoinPath(stagingDirectory, serialText + "_fragment.blob");
		snapshot.vertexShaderProfile = StringHelper::ShaderProfileForType(ShaderTarget::VertexShader);
		snapshot.fragmentShaderProfile = StringHelper::ShaderProfileForType(ShaderTarget::PixelShader);
		if (RenderPass::ResolveExecutionMode(snapshot) == RenderPass::ExecutionMode::Compute)
			snapshot.fragmentShaderProfile = StringHelper::ShaderProfileForType(ShaderTarget::ComputeShader);

		std::vector<std::string> compilerArguments;
		if (RenderPass::IsReplacementPass(snapshot.type))
		{
			const ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = ResolveModifiedShader(snapshot);
			if (modifiedShader)
				compilerArguments = modifiedShader->compilerArguments;
		}

		const uint64_t generation = gRenderPassRefreshGeneration;
		gPendingRenderPassCompiles[renderPassId] = {serial, generation, {}};
		{
			std::lock_guard<std::mutex> lock(gRenderPassCompileMutex);
			++gRenderPassCompileProgress.total;
			++gRenderPassCompileProgress.queued;
		}

		const bool queued = DatabaseModifiedShaders::QueueBackgroundCompile(
			[snapshot = std::move(snapshot), compilerArguments = std::move(compilerArguments), renderPassId, fingerprint, packageDirectory, jsonPath, generation, serial]() mutable
			{
				{
					std::lock_guard<std::mutex> lock(gRenderPassCompileMutex);
					--gRenderPassCompileProgress.queued;
					++gRenderPassCompileProgress.running;
				}

				CompletedRenderPassCompile result;
				result.id = renderPassId;
				result.fingerprint = fingerprint;
				result.packageDirectory = packageDirectory;
				result.jsonPath = jsonPath;
				result.generation = generation;
				result.serial = serial;
				result.snapshot = std::move(snapshot);
				result.compilerArguments = compilerArguments;
				try
				{
					result.succeeded = RenderPassShaders::CompilePreparedShaders(result.snapshot, compilerArguments, result.error);
				}
				catch (const std::exception& exception)
				{
					result.error = exception.what();
				}
				catch (...)
				{
					result.error = "Unknown render pass compile error.";
				}

				std::lock_guard<std::mutex> lock(gRenderPassCompileMutex);
				--gRenderPassCompileProgress.running;
				gCompletedRenderPassCompiles.push_back(std::move(result));
			});

		if (!queued)
		{
			gPendingRenderPassCompiles.erase(renderPassId);
			std::lock_guard<std::mutex> lock(gRenderPassCompileMutex);
			--gRenderPassCompileProgress.total;
			--gRenderPassCompileProgress.queued;
			outError = "Render Pass compile worker is shutting down.";
			return false;
		}

		return true;
	}

	RenderPassShaderBatchCompileResult QueueAllRenderPassShaderCompiles()
	{
		RenderPassShaderBatchCompileResult result;
		EnsureRenderPassesLoaded();
		for (const RenderPass::RenderPassDisk& renderPass : GetRenderPasses())
		{
			std::string error;
			if (QueueRenderPassShaderCompile(renderPass.id, error))
				++result.compiledRenderPassCount;
			else
			{
				if (error == "Render Pass shader compilation is already queued." || error == "Render Pass shader source files are missing.")
					++result.skippedRenderPassCount;
				else
				{
					++result.failedRenderPassCount;
					result.errors.push_back(renderPass.id + ": " + error);
				}
			}
		}
		return result;
	}

	RenderPassCompileProgress GetRenderPassCompileProgress()
	{
		std::lock_guard<std::mutex> lock(gRenderPassCompileMutex);
		return gRenderPassCompileProgress;
	}

	void ProcessCompletedRenderPassCompiles()
	{
		std::vector<CompletedRenderPassCompile> results;
		{
			std::lock_guard<std::mutex> lock(gRenderPassCompileMutex);
			results.swap(gCompletedRenderPassCompiles);
		}

		bool acceptedResult = false;
		for (CompletedRenderPassCompile& result : results)
		{
			for (const std::string& stagingPath : {result.snapshot.vertexShaderCompiledBlobPath, result.snapshot.fragmentShaderCompiledBlobPath})
			{
				if (!stagingPath.empty())
					ShaderInjectorIO::DeleteFileIfExists(stagingPath);
			}

			RenderPass::RenderPassDisk* livePass = FindRenderPassById(result.id);
			std::vector<std::string> currentCompilerArguments;

			if (livePass && RenderPass::IsReplacementPass(livePass->type))
			{
				const ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = ResolveModifiedShader(*livePass);

				if (modifiedShader)
					currentCompilerArguments = modifiedShader->compilerArguments;
			}

			const bool current = livePass && result.generation == gRenderPassRefreshGeneration &&
				livePass->packageDirectory == result.packageDirectory && livePass->jsonPath == result.jsonPath &&
				ConfigurationFingerprint(*livePass) == result.fingerprint &&
				currentCompilerArguments == result.compilerArguments;
			if (!current)
			{
				{
					std::lock_guard<std::mutex> lock(gRenderPassCompileMutex);
					++gRenderPassCompileProgress.discarded;
					++gRenderPassCompileProgress.completed;
					gPendingRenderPassCompiles.erase(result.id);
				}
				ShaderInjectorGUI::WriteToRuntimeLogWarning("Render pass compile result discarded as stale: " + result.id);
				continue;
			}

			if (!result.succeeded)
			{
				{
					std::lock_guard<std::mutex> lock(gRenderPassCompileMutex);
					++gRenderPassCompileProgress.failed;
					++gRenderPassCompileProgress.completed;
					gPendingRenderPassCompiles.erase(result.id);
				}
				ShaderInjectorGUI::WriteToRuntimeLogError("Render pass compile failed for " + result.id + ": " + result.error);
				continue;
			}

			RenderPass::RenderPassDisk candidate = *livePass;
			candidate.vertexShaderBlob = std::move(result.snapshot.vertexShaderBlob);
			candidate.fragmentShaderBlob = std::move(result.snapshot.fragmentShaderBlob);
			candidate.vertexShaderBlobHash = result.snapshot.vertexShaderBlobHash;
			candidate.fragmentShaderBlobHash = result.snapshot.fragmentShaderBlobHash;
			candidate.vertexShaderProfile = result.snapshot.vertexShaderProfile;
			candidate.fragmentShaderProfile = result.snapshot.fragmentShaderProfile;
			bool wrote = true;
			if (!result.snapshot.vertexShaderCompiledBlobPath.empty())
				wrote = ShaderInjectorIO::WriteBinaryFile(candidate.vertexShaderCompiledBlobPath, candidate.vertexShaderBlob.data(), candidate.vertexShaderBlob.size());
			if (wrote)
				wrote = ShaderInjectorIO::WriteBinaryFile(candidate.fragmentShaderCompiledBlobPath, candidate.fragmentShaderBlob.data(), candidate.fragmentShaderBlob.size());
			if (wrote)
				wrote = RenderPass::WriteJson(candidate);
			if (!wrote)
			{
				{
					std::lock_guard<std::mutex> lock(gRenderPassCompileMutex);
					++gRenderPassCompileProgress.failed;
					++gRenderPassCompileProgress.completed;
					gPendingRenderPassCompiles.erase(result.id);
				}
				ShaderInjectorGUI::WriteToRuntimeLogError("Could not activate compiled render pass " + result.id);
				continue;
			}

			const std::vector<ModifiedShader::ModifiedShaderCompiledVariant> previousVariants = std::move(candidate.fragmentShaderVariants);
			*livePass = std::move(candidate);
			RenderPassShaders::LoadShaderVariants(*livePass);

			//keep an in-memory working variant if its cache file went missing while the new compile was queued.
			for (ModifiedShader::ModifiedShaderCompiledVariant& variant : livePass->fragmentShaderVariants)
			{
				if (!variant.compiledBlob.empty())
					continue;

				for (const ModifiedShader::ModifiedShaderCompiledVariant& previousVariant : previousVariants)
				{
					if (previousVariant.defines != variant.defines || !previousVariant.compiledShaderInterfaceCompatible)
						continue;

					variant.compiledBlob = previousVariant.compiledBlob;
					variant.compiledShaderAnalysis = previousVariant.compiledShaderAnalysis;
					variant.compiledShaderInterfaceCompatible = previousVariant.compiledShaderInterfaceCompatible;
					break;
				}
			}

			const ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = ResolveModifiedShader(*livePass);
			int variantCount = 0;
			if (modifiedShader && RenderPass::IsReplacementPass(livePass->type))
				variantCount = DatabaseModifiedShaders::QueueSourceCompiles(*modifiedShader, livePass->id, livePass->fragmentShaderSourcePath, livePass->fragmentShaderVariants);

			{
				std::lock_guard<std::mutex> lock(gRenderPassCompileMutex);
				++gRenderPassCompileProgress.completed;
				gRenderPassCompileProgress.total += variantCount;
				gRenderPassCompileProgress.queued += variantCount;
				PendingRenderPassCompile& pending = gPendingRenderPassCompiles[result.id];
				pending.serial = result.serial;
				pending.generation = result.generation;
				pending.variants.clear();
				for (const ModifiedShader::ModifiedShaderCompiledVariant& variant : livePass->fragmentShaderVariants)
				{
					if (variant.pendingCompileSerial != 0)
						pending.variants.emplace_back(variant.defines, variant.pendingCompileSerial);
				}
				if (variantCount == 0)
					gPendingRenderPassCompiles.erase(result.id);
			}
			acceptedResult = true;
			ShaderInjectorGUI::WriteToRuntimeLogSuccess("Compiled render pass shaders: " + result.id);
		}

		for (auto pending = gPendingRenderPassCompiles.begin(); pending != gPendingRenderPassCompiles.end();)
		{
			const std::string renderPassId = pending->first;
			const uint64_t serial = pending->second.serial;
			++pending;
			CountVariantCompletions(renderPassId, serial);
		}

		if (acceptedResult)
			PublishRuntimeConfiguration();

		RenderPassCompileProgress finishedProgress;
		{
			std::lock_guard<std::mutex> lock(gRenderPassCompileMutex);
			if (gRenderPassCompileProgress.total > 0 && gRenderPassCompileProgress.completed >= gRenderPassCompileProgress.total &&
				gRenderPassCompileProgress.running == 0 && gRenderPassCompileProgress.queued == 0 && gPendingRenderPassCompiles.empty())
			{
				finishedProgress = gRenderPassCompileProgress;
				gRenderPassCompileProgress = {};
			}
		}

		if (finishedProgress.total > 0)
		{
			const std::string summary = "Render Pass shader compile batch finished: completed=" + std::to_string(finishedProgress.completed) +
				", failed=" + std::to_string(finishedProgress.failed) + ", discarded=" + std::to_string(finishedProgress.discarded);

			if (finishedProgress.failed > 0)
				ShaderInjectorGUI::WriteToRuntimeLogError(summary);
			else if (finishedProgress.discarded > 0)
				ShaderInjectorGUI::WriteToRuntimeLogWarning(summary);
			else
				ShaderInjectorGUI::WriteToRuntimeLogSuccess(summary);
		}
	}

	void InvalidateRenderPassCompiles()
	{
		++gRenderPassRefreshGeneration;
	}
} //namespace DatabaseRenderPasses
