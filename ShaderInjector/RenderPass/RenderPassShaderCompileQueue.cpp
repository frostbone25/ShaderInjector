#include "RenderPass/RenderPassShaderCompileQueue.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "GUI/ShaderInjectorGUI.h"
#include "IO/ShaderInjectorIO.h"
#include "ModifiedShader/DatabaseModifiedShaders.h"
#include "RenderPass/RenderPassShaders.h"
#include "StringHelper.h"
#include "ShaderConfiguration/DatabaseShaderConfigurations.h"
#include "ShaderConfiguration/ConfigurationProgressTask.h"

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
			std::string stagedJsonPath;
			uint64_t generation = 0;
			uint64_t serial = 0;
			RenderPass::RenderPassDisk snapshot;
			std::vector<std::string> compilerArguments;
			std::string error;
			bool succeeded = false;
			bool urgent = false;
			std::shared_ptr<const ShaderConfiguration::CompileConfiguration> configuration;
			std::shared_ptr<const ShaderConfiguration::CompileConfiguration> previousConfiguration;
			std::shared_ptr<ShaderConfiguration::ConfigurationProgressTask> progressTask;
		};

		struct PendingRenderPassCompile
		{
			uint64_t serial = 0;
			uint64_t generation = 0;
			std::string configurationFingerprint;
			std::shared_ptr<ShaderConfiguration::ConfigurationProgress> progress;
			std::shared_ptr<std::atomic<bool>> baseStarted;
			bool variantFailed = false;
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

		std::string CompileConfigurationFingerprint(const RenderPass::RenderPassDisk& renderPass, const std::shared_ptr<const ShaderConfiguration::CompileConfiguration>& configuration)
		{
			if (!configuration)
				return {};

			std::string fingerprint;
			size_t stageIndex = 0;

			//all shader stages belong to one atomic pass update, including both halves of a mip chain.
			for (const std::string& sourcePath : {renderPass.vertexShaderSourcePath, renderPass.fragmentShaderSourcePath, renderPass.upsampleShaderSourcePath})
			{
				if (!sourcePath.empty())
				{
					const std::string sourceFingerprint = configuration->GetSourceFingerprint(sourcePath);

					if (!sourceFingerprint.empty())
						fingerprint += std::to_string(stageIndex) + ":" + sourceFingerprint + "\n";
				}

				++stageIndex;
			}

			return fingerprint;
		}

		std::string SerializePortableRenderPass(const RenderPass::RenderPassDisk& renderPass)
		{
			if (renderPass.jsonPath.empty() || renderPass.id.empty() ||
				(renderPass.type == RenderPass::RenderPassType::DownsampleUpsample && !RenderPass::IsValidDownsampleUpsampleFactor(renderPass.chainDownsampleFactor)))
				return {};

			RenderPass::RenderPassDisk portableRenderPass = renderPass;
			RenderPass::NormalizeExecutionResources(portableRenderPass);
			portableRenderPass.packageDirectory.clear();
			portableRenderPass.jsonPath.clear();
			portableRenderPass.vertexShaderSourcePath.clear();
			portableRenderPass.fragmentShaderSourcePath.clear();
			portableRenderPass.vertexShaderCompiledBlobPath.clear();
			portableRenderPass.fragmentShaderCompiledBlobPath.clear();
			portableRenderPass.upsampleShaderSourcePath.clear();
			portableRenderPass.upsampleShaderCompiledBlobPath.clear();
			portableRenderPass.vertexShaderBlob.clear();
			portableRenderPass.fragmentShaderBlob.clear();
			portableRenderPass.upsampleShaderBlob.clear();
			portableRenderPass.vertexShaderBlobHash = 0;
			portableRenderPass.fragmentShaderBlobHash = 0;
			portableRenderPass.upsampleShaderBlobHash = 0;
			return nlohmann::ordered_json(portableRenderPass).dump(4);
		}

		void ScheduleStagingCleanup(std::vector<std::string> stagingPaths)
		{
			stagingPaths.erase(
				std::remove_if(stagingPaths.begin(), stagingPaths.end(), [](const std::string& path) { return path.empty(); }),
				stagingPaths.end());

			if (stagingPaths.empty())
				return;

			DatabaseModifiedShaders::QueueBackgroundCompile(
				[stagingPaths = std::move(stagingPaths)]()
				{
					for (const std::string& stagingPath : stagingPaths)
						ShaderInjectorIO::DeleteFileIfExists(stagingPath);
				},
				false);
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
				{
					pending.variantFailed = true;
					++gRenderPassCompileProgress.failed;
				}
				++gRenderPassCompileProgress.completed;
				--gRenderPassCompileProgress.queued;
				variantIt = pending.variants.erase(variantIt);
			}

			if (pending.variants.empty())
			{
				if (!pending.variantFailed && (!pending.progress || !pending.progress->IsCancellationRequested()))
					DatabaseModifiedShaders::ClearInterruptedCompiles(renderPassId);

				gPendingRenderPassCompiles.erase(renderPassId);
			}
		}
	}

	bool QueueRenderPassShaderCompile(const std::string& renderPassId, std::string& outError, std::shared_ptr<const ShaderConfiguration::CompileConfiguration> previousConfiguration)
	{
		outError.clear();
		EnsureRenderPassesLoaded();

		RenderPass::RenderPassDisk* livePass = FindRenderPassById(renderPassId);
		if (!livePass)
		{
			outError = "Render Pass was not found.";
			return false;
		}
		RenderPass::ResolveShaderPaths(*livePass);
		const auto configuration = DatabaseShaderConfigurations::GetAppliedConfiguration();
		const std::string compileConfigurationFingerprint = CompileConfigurationFingerprint(*livePass, configuration);
		std::shared_ptr<ShaderConfiguration::ConfigurationProgress> configurationProgress;

		if (configuration)
			configurationProgress = configuration->GetProgress();

		if (configurationProgress && configurationProgress->IsCancellationRequested())
		{
			outError = "Shader configuration compilation was superseded.";
			return false;
		}

		auto pendingCompile = gPendingRenderPassCompiles.find(renderPassId);
		if (pendingCompile != gPendingRenderPassCompiles.end() && pendingCompile->second.configurationFingerprint == compileConfigurationFingerprint)
		{
			const bool newTrackedOperation = configurationProgress && configurationProgress != pendingCompile->second.progress;

			if (!newTrackedOperation)
			{
				outError = "Render Pass shader compilation is already queued.";
				return false;
			}
		}

		if (!RenderPass::HasShaderTemplate(*livePass))
		{
			outError = "Render Pass shader source files are missing.";
			return false;
		}

		std::shared_ptr<ShaderConfiguration::ConfigurationProgressTask> progressTask;

		if (configurationProgress)
			progressTask = configurationProgress->BeginTask(livePass->enabled);

		RenderPass::RenderPassDisk snapshot = *livePass;
		const std::string fingerprint = ConfigurationFingerprint(*livePass);
		const std::string packageDirectory = livePass->packageDirectory;
		const std::string jsonPath = livePass->jsonPath;
		const uint64_t serial = gNextRenderPassCompileSerial.fetch_add(1);
		const std::string stagingDirectory = ShaderInjectorIO::JoinPath(packageDirectory, ".shader-compile-staging");

		const std::string serialText = std::to_string(serial);
		const std::string durableNameToken = std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "_" + serialText;
		const std::string stagedJsonPath = jsonPath + ".compile." + serialText + ".stage";
		const bool compileVertexShader = !snapshot.vertexShaderSourcePath.empty();
		snapshot.vertexShaderCompiledBlobPath.clear();
		if (compileVertexShader)
		{
			snapshot.vertexShaderCompiledBlobFile = durableNameToken + "_vertex.blob";
			snapshot.vertexShaderCompiledBlobPath = ShaderInjectorIO::JoinPath(stagingDirectory, serialText + "_vertex.blob");
		}
		snapshot.fragmentShaderCompiledBlobFile = durableNameToken + "_fragment.blob";
		snapshot.fragmentShaderCompiledBlobPath = ShaderInjectorIO::JoinPath(stagingDirectory, serialText + "_fragment.blob");
		if (snapshot.type == RenderPass::RenderPassType::DownsampleUpsample)
		{
			snapshot.upsampleShaderCompiledBlobFile = durableNameToken + "_upsample.blob";
			snapshot.upsampleShaderCompiledBlobPath = ShaderInjectorIO::JoinPath(stagingDirectory, serialText + "_upsample.blob");
		}
		snapshot.vertexShaderProfile = StringHelper::ShaderProfileForType(ShaderTarget::VertexShader);
		snapshot.fragmentShaderProfile = StringHelper::ShaderProfileForType(ShaderTarget::PixelShader);
		if (RenderPass::ResolveExecutionMode(snapshot) == RenderPass::ExecutionMode::Compute)
			snapshot.fragmentShaderProfile = StringHelper::ShaderProfileForType(ShaderTarget::ComputeShader);
		snapshot.upsampleShaderProfile = snapshot.fragmentShaderProfile;

		std::vector<std::string> compilerArguments;
		if (RenderPass::IsReplacementPass(snapshot.type))
		{
			const ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = ResolveModifiedShader(snapshot);
			if (modifiedShader)
				compilerArguments = modifiedShader->compilerArguments;
		}

		const uint64_t generation = gRenderPassRefreshGeneration;
		const bool urgent = livePass->enabled;
		PendingRenderPassCompile pendingCompileSnapshot;
		pendingCompileSnapshot.serial = serial;
		pendingCompileSnapshot.generation = generation;
		pendingCompileSnapshot.configurationFingerprint = compileConfigurationFingerprint;
		pendingCompileSnapshot.progress = configurationProgress;
		const auto baseStarted = std::make_shared<std::atomic<bool>>(false);
		pendingCompileSnapshot.baseStarted = baseStarted;
		gPendingRenderPassCompiles[renderPassId] = std::move(pendingCompileSnapshot);
		{
			std::lock_guard<std::mutex> lock(gRenderPassCompileMutex);
			++gRenderPassCompileProgress.total;
			++gRenderPassCompileProgress.queued;
		}

		const bool queued = DatabaseModifiedShaders::QueueBackgroundCompile(
			[snapshot = std::move(snapshot), compilerArguments = std::move(compilerArguments), configuration, previousConfiguration, configurationProgress, baseStarted, progressTask = std::move(progressTask), renderPassId, fingerprint, packageDirectory, jsonPath, stagedJsonPath, stagingDirectory, generation, serial, urgent]() mutable
			{
				{
					std::lock_guard<std::mutex> lock(gRenderPassCompileMutex);

					//a canceled queued pass never enters the native compiler or changes running counters.
					if (configurationProgress && configurationProgress->IsCancellationRequested())
						return;

					baseStarted->store(true);
					--gRenderPassCompileProgress.queued;
					++gRenderPassCompileProgress.running;
				}

				CompletedRenderPassCompile result;
				result.id = renderPassId;
				result.fingerprint = fingerprint;
				result.packageDirectory = packageDirectory;
				result.jsonPath = jsonPath;
				result.stagedJsonPath = stagedJsonPath;
				result.generation = generation;
				result.serial = serial;
				result.urgent = urgent;
				result.configuration = configuration;
				result.previousConfiguration = previousConfiguration;
				result.progressTask = std::move(progressTask);
				result.snapshot = std::move(snapshot);
				result.compilerArguments = compilerArguments;
				try
				{
					ShaderInjectorIO::DirectoryCreate(stagingDirectory);
					result.succeeded = RenderPassShaders::CompilePreparedShaders(result.snapshot, compilerArguments, result.error, configuration, previousConfiguration);
					if (result.succeeded)
					{
						const std::string stagedMetadata = SerializePortableRenderPass(result.snapshot);
						if (stagedMetadata.empty() || !ShaderInjectorIO::WriteTextFile(result.stagedJsonPath, stagedMetadata))
						{
							result.succeeded = false;
							result.error = "Could not prepare the render pass metadata for publication.";
						}
					}
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
			}, livePass->enabled, configurationProgress);

		if (!queued)
		{
			const auto pending = gPendingRenderPassCompiles.find(renderPassId);
			if (pending != gPendingRenderPassCompiles.end() && pending->second.serial == serial)
				gPendingRenderPassCompiles.erase(pending);
			std::lock_guard<std::mutex> lock(gRenderPassCompileMutex);
			--gRenderPassCompileProgress.total;
			--gRenderPassCompileProgress.queued;
			outError = "Render Pass compile worker is shutting down.";
			return false;
		}

		return true;
	}

	RenderPassShaderBatchCompileResult QueueAllRenderPassShaderCompiles(std::shared_ptr<const ShaderConfiguration::CompileConfiguration> previousConfiguration)
	{
		RenderPassShaderBatchCompileResult result;
		EnsureRenderPassesLoaded();
		const auto configuration = DatabaseShaderConfigurations::GetAppliedConfiguration();

		for (const RenderPass::RenderPassDisk& renderPass : GetRenderPasses())
		{
			const bool interrupted = DatabaseModifiedShaders::HasInterruptedCompiles(renderPass.id);

			if (previousConfiguration && !interrupted)
			{
				RenderPass::RenderPassDisk resolvedPass = renderPass;
				RenderPass::ResolveShaderPaths(resolvedPass);

				//framework properties still publish runtime settings without recompiling unrelated HLSL.
				if (CompileConfigurationFingerprint(resolvedPass, previousConfiguration) == CompileConfigurationFingerprint(resolvedPass, configuration))
				{
					++result.skippedRenderPassCount;
					continue;
				}
			}

			std::string error;
			auto passPreviousConfiguration = previousConfiguration;

			//an interrupted pass may still hold older bytecode even when this edit leaves its properties unchanged.
			if (interrupted)
				passPreviousConfiguration.reset();

			if (QueueRenderPassShaderCompile(renderPass.id, error, passPreviousConfiguration))
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
			if (!gCompletedRenderPassCompiles.empty())
			{
				auto nextResult = std::find_if(
					gCompletedRenderPassCompiles.begin(),
					gCompletedRenderPassCompiles.end(),
					[](const CompletedRenderPassCompile& result) { return result.urgent; });

				if (nextResult == gCompletedRenderPassCompiles.end())
					nextResult = gCompletedRenderPassCompiles.begin();

				results.push_back(std::move(*nextResult));
				gCompletedRenderPassCompiles.erase(nextResult);
			}
		}

		bool acceptedResult = false;
		for (CompletedRenderPassCompile& result : results)
		{
			const std::vector<std::string> stagingPaths = {
				result.snapshot.vertexShaderCompiledBlobPath,
				result.snapshot.fragmentShaderCompiledBlobPath,
				result.snapshot.upsampleShaderCompiledBlobPath,
				result.stagedJsonPath};

			//operation identity takes precedence over equal source fingerprints, including an A-to-B-to-A edit.
			if (result.configuration && result.configuration->GetProgress() && result.configuration->GetProgress()->IsCancellationRequested())
			{
				ScheduleStagingCleanup(stagingPaths);
				continue;
			}

			RenderPass::RenderPassDisk* livePass = FindRenderPassById(result.id);
			std::vector<std::string> currentCompilerArguments;

			if (livePass && RenderPass::IsReplacementPass(livePass->type))
			{
				const ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = ResolveModifiedShader(*livePass);

				if (modifiedShader)
					currentCompilerArguments = modifiedShader->compilerArguments;
			}
			const auto currentConfiguration = DatabaseShaderConfigurations::GetAppliedConfiguration();
			const std::string currentCompileConfigurationFingerprint = CompileConfigurationFingerprint(result.snapshot, currentConfiguration);
			const std::string resultCompileConfigurationFingerprint = CompileConfigurationFingerprint(result.snapshot, result.configuration);
			bool pendingRequestIsCurrent = false;

			{
				std::lock_guard<std::mutex> lock(gRenderPassCompileMutex);
				const auto pending = gPendingRenderPassCompiles.find(result.id);
				pendingRequestIsCurrent = pending != gPendingRenderPassCompiles.end() && pending->second.serial == result.serial;
			}

			const bool current = pendingRequestIsCurrent && livePass && result.generation == gRenderPassRefreshGeneration &&
				livePass->packageDirectory == result.packageDirectory && livePass->jsonPath == result.jsonPath &&
				ConfigurationFingerprint(*livePass) == result.fingerprint &&
				currentCompileConfigurationFingerprint == resultCompileConfigurationFingerprint &&
				currentCompilerArguments == result.compilerArguments;
			if (!current)
			{
				{
					std::lock_guard<std::mutex> lock(gRenderPassCompileMutex);
					++gRenderPassCompileProgress.discarded;
					++gRenderPassCompileProgress.completed;
					auto pending = gPendingRenderPassCompiles.find(result.id);
					if (pending != gPendingRenderPassCompiles.end() && pending->second.serial == result.serial)
						gPendingRenderPassCompiles.erase(pending);
				}
				ShaderInjectorGUI::WriteToRuntimeLogWarning("Render pass compile result discarded as stale: " + result.id);
				ScheduleStagingCleanup(stagingPaths);
				continue;
			}

			if (!result.succeeded)
			{
				if (result.progressTask)
					result.progressTask->MarkFailed();

				{
					std::lock_guard<std::mutex> lock(gRenderPassCompileMutex);
					++gRenderPassCompileProgress.failed;
					++gRenderPassCompileProgress.completed;
					auto pending = gPendingRenderPassCompiles.find(result.id);
					if (pending != gPendingRenderPassCompiles.end() && pending->second.serial == result.serial)
						gPendingRenderPassCompiles.erase(pending);
				}
				ShaderInjectorGUI::WriteToRuntimeLogError("Render pass compile failed for " + result.id + ": " + result.error);
				ScheduleStagingCleanup(stagingPaths);
				continue;
			}

			RenderPass::RenderPassDisk candidate = *livePass;
			candidate.vertexShaderBlob = std::move(result.snapshot.vertexShaderBlob);
			candidate.fragmentShaderBlob = std::move(result.snapshot.fragmentShaderBlob);
			candidate.upsampleShaderBlob = std::move(result.snapshot.upsampleShaderBlob);
			candidate.vertexShaderBlobHash = result.snapshot.vertexShaderBlobHash;
			candidate.fragmentShaderBlobHash = result.snapshot.fragmentShaderBlobHash;
			candidate.upsampleShaderBlobHash = result.snapshot.upsampleShaderBlobHash;
			candidate.vertexShaderProfile = result.snapshot.vertexShaderProfile;
			candidate.fragmentShaderProfile = result.snapshot.fragmentShaderProfile;
			candidate.upsampleShaderProfile = result.snapshot.upsampleShaderProfile;
			candidate.vertexShaderCompiledBlobFile = result.snapshot.vertexShaderCompiledBlobFile;
			candidate.fragmentShaderCompiledBlobFile = result.snapshot.fragmentShaderCompiledBlobFile;
			candidate.upsampleShaderCompiledBlobFile = result.snapshot.upsampleShaderCompiledBlobFile;
			RenderPass::ResolveShaderPaths(candidate);
			bool wrote = true;
			if (!result.snapshot.vertexShaderCompiledBlobPath.empty())
				wrote = ShaderInjectorIO::CommitStagedFile(result.snapshot.vertexShaderCompiledBlobPath, candidate.vertexShaderCompiledBlobPath);
			if (wrote)
				wrote = ShaderInjectorIO::CommitStagedFile(result.snapshot.fragmentShaderCompiledBlobPath, candidate.fragmentShaderCompiledBlobPath);
			if (wrote && candidate.type == RenderPass::RenderPassType::DownsampleUpsample)
				wrote = ShaderInjectorIO::CommitStagedFile(result.snapshot.upsampleShaderCompiledBlobPath, candidate.upsampleShaderCompiledBlobPath);
			if (wrote)
				wrote = ShaderInjectorIO::CommitStagedFile(result.stagedJsonPath, candidate.jsonPath);
			if (!wrote)
			{
				if (result.progressTask)
					result.progressTask->MarkFailed();

				{
					std::lock_guard<std::mutex> lock(gRenderPassCompileMutex);
					++gRenderPassCompileProgress.failed;
					++gRenderPassCompileProgress.completed;
					auto pending = gPendingRenderPassCompiles.find(result.id);
					if (pending != gPendingRenderPassCompiles.end() && pending->second.serial == result.serial)
						gPendingRenderPassCompiles.erase(pending);
				}
				ShaderInjectorGUI::WriteToRuntimeLogError("Could not activate compiled render pass " + result.id);
				ScheduleStagingCleanup(stagingPaths);
				continue;
			}

			const std::vector<ModifiedShader::ModifiedShaderCompiledVariant> previousVariants = std::move(candidate.fragmentShaderVariants);
			*livePass = std::move(candidate);
			RenderPassShaders::LoadShaderVariants(*livePass, false);

			//reuse the prior in-memory blob while new source variants compile in the background.
			for (ModifiedShader::ModifiedShaderCompiledVariant& variant : livePass->fragmentShaderVariants)
			{
				if (!variant.compiledBlob.empty())
					continue;

				for (const ModifiedShader::ModifiedShaderCompiledVariant& previousVariant : previousVariants)
				{
					if (previousVariant.defines != variant.defines || previousVariant.compiledBlob.empty() || !previousVariant.compiledShaderInterfaceCompatible)
						continue;

					variant.compiledBlob = previousVariant.compiledBlob;
					variant.compiledShaderAnalysis = previousVariant.compiledShaderAnalysis;
					variant.compiledShaderInterfaceCompatible = previousVariant.compiledShaderInterfaceCompatible;
					variant.compiledConfigurationFingerprint = previousVariant.compiledConfigurationFingerprint;
					break;
				}
			}

			const ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = ResolveModifiedShader(*livePass);
			int variantCount = 0;
			bool fragmentConfigurationChanged = true;

			if (result.previousConfiguration && result.configuration)
				fragmentConfigurationChanged = result.previousConfiguration->GetSourceFingerprint(livePass->fragmentShaderSourcePath) != result.configuration->GetSourceFingerprint(livePass->fragmentShaderSourcePath);

			//a vertex-only property edit must not trigger every unchanged pixel-shader variant.
			if (modifiedShader && RenderPass::IsReplacementPass(livePass->type) && fragmentConfigurationChanged)
				variantCount = DatabaseModifiedShaders::QueueSourceCompiles(*modifiedShader, livePass->id, livePass->fragmentShaderSourcePath, livePass->fragmentShaderVariants, result.configuration);

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
				{
					DatabaseModifiedShaders::ClearInterruptedCompiles(result.id);
					auto pending = gPendingRenderPassCompiles.find(result.id);
					if (pending != gPendingRenderPassCompiles.end() && pending->second.serial == result.serial)
						gPendingRenderPassCompiles.erase(pending);
				}
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
			RequestRuntimeShaderPublication(!results.empty() && results.front().urgent, results.front().progressTask);

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

	void CancelConfigurationCompiles(std::shared_ptr<ShaderConfiguration::ConfigurationProgress> progress)
	{
		if (!progress)
			return;

		progress->Cancel();
		std::lock_guard<std::mutex> lock(gRenderPassCompileMutex);

		for (auto pending = gPendingRenderPassCompiles.begin(); pending != gPendingRenderPassCompiles.end();)
		{
			if (pending->second.progress != progress)
			{
				++pending;
				continue;
			}

			DatabaseModifiedShaders::MarkInterruptedCompile(pending->first);
			const int variantCount = static_cast<int>(pending->second.variants.size());
			int canceledCount = variantCount;

			if (variantCount == 0)
			{
				canceledCount = 1;

				if (!pending->second.baseStarted || !pending->second.baseStarted->load())
					--gRenderPassCompileProgress.queued;
			}
			else
				gRenderPassCompileProgress.queued -= variantCount;

			//a running native call releases its running count when it returns; its result will be discarded.
			gRenderPassCompileProgress.completed += canceledCount;
			gRenderPassCompileProgress.discarded += canceledCount;
			pending = gPendingRenderPassCompiles.erase(pending);
		}
	}

	void InvalidateRenderPassCompiles()
	{
		++gRenderPassRefreshGeneration;
	}
} //namespace DatabaseRenderPasses
