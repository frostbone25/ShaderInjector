#include "DatabaseModifiedShaders.h"
#include "DatabaseModifiedShadersInternal.h"
#include "ModifiedShaderCompilePriority.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#endif

#include "GUI/ShaderInjectorGUI.h"
#include "Hash/Hash.h"
#include "HookD3D12/HookD3D12.h"
#include "IO/ShaderInjectorIO.h"
#include "RenderPass/DatabaseRenderPasses.h"
#include "ShaderConfiguration/DatabaseShaderConfigurations.h"
#include "ShaderConfiguration/ConfigurationProgressTask.h"

namespace DatabaseModifiedShaders
{
	std::mutex Detail::gCompileMutex;

	struct CompileJob
	{
		std::shared_ptr<const ModifiedShader::ModifiedShaderPackageDisk> package;
		std::vector<ModifiedShader::ShaderDefine> defines;
		std::string key;
		std::string candidateBasePath;
		std::string compiledBlobPath;
		std::string sourcePath;
		bool urgent = false;
		uint64_t serial = 0;
		std::shared_ptr<const ShaderConfiguration::CompileConfiguration> configuration;
		std::shared_ptr<ShaderConfiguration::ConfigurationProgressTask> progressTask;
		bool retryInterruptedOwner = false;
		uint64_t operationID = 0;
	};

	struct CompileResult
	{
		std::string packageId;
		std::vector<ModifiedShader::ShaderDefine> defines;
		std::string compiledBlobPath;
		std::string sourcePath;
		std::string stagedBlobPath;
		uint64_t serial = 0;
		bool succeeded = false;
		bool urgent = false;
		bool published = false;
		std::shared_ptr<const ShaderConfiguration::CompileConfiguration> configuration;
		std::shared_ptr<ShaderConfiguration::ConfigurationProgressTask> progressTask;
		std::vector<uint8_t> blob;
		ShaderAnalysis::ShaderAnalysisDisk analysis;
		bool retryInterruptedOwner = false;
		bool retryFailed = false;
		uint64_t operationID = 0;
	};

	struct BackgroundCompileTask
	{
		std::function<void()> task;
		std::shared_ptr<ShaderConfiguration::ConfigurationProgress> progress;
		bool urgent = false;
	};

	struct InterruptedOwnerRetry
	{
		uint64_t operationID = 0;
		size_t pendingCompileCount = 0;
		bool failed = false;
	};

	struct DeferredVariantRequest
	{
		std::string packageId;
		std::vector<ModifiedShader::ShaderDefine> defines;
		std::shared_ptr<const ShaderConfiguration::CompileConfiguration> configuration;
	};

	static std::condition_variable gCompileCondition;
	static std::deque<CompileJob> gCompileQueue;
	static std::deque<BackgroundCompileTask> gBackgroundCompileTasks;
	static std::deque<BackgroundCompileTask> gUrgentBackgroundCompileTasks;
	static std::vector<CompileResult> gCompileResults;
	static CompileProgress gCompileProgress;
	static std::unordered_map<std::string, CompileJob> gCompileInFlightJobs;
	static std::unordered_set<std::string> gInterruptedCompileOwners;
	static std::unordered_map<std::string, InterruptedOwnerRetry> gInterruptedOwnerRetries;
	static size_t gUrgentBackgroundRunningCount = 0;
	static uint64_t gNextCompileSerial = 1;
	//leaked: a joinable thread destroyed at process exit calls std::terminate.
	static std::vector<std::thread>& gCompileWorkers = *new std::vector<std::thread>();
	static bool gCompileStopRequested = false;

	static std::vector<DeferredVariantRequest> gDeferredVariantRequests;
	static std::unordered_set<std::string> gDeferredVariantRequestKeys;
	static ModifiedShader::VariantUseTracker gVariantUseTracker;

	static void CompleteInterruptedOwnerRetryLocked(const CompileResult& result)
	{
		if (!result.retryInterruptedOwner)
			return;

		auto retry = gInterruptedOwnerRetries.find(result.packageId);
		if (retry == gInterruptedOwnerRetries.end() || retry->second.operationID != result.operationID)
			return;

		if (result.retryFailed)
			retry->second.failed = true;

		if (retry->second.pendingCompileCount > 0)
			--retry->second.pendingCompileCount;

		if (retry->second.pendingCompileCount != 0)
			return;

		if (!retry->second.failed)
			gInterruptedCompileOwners.erase(result.packageId);

		gInterruptedOwnerRetries.erase(retry);
	}

	static std::string InFlightCompileJobKey(const CompileJob& job);

	static bool HasUrgentWorkLocked()
	{
		if (!gCompileQueue.empty() && gCompileQueue.front().urgent)
			return true;

		if (!gUrgentBackgroundCompileTasks.empty() || gUrgentBackgroundRunningCount != 0)
			return true;

		return std::any_of(gCompileInFlightJobs.begin(), gCompileInFlightJobs.end(), [](const auto& inFlightEntry)
		{
			return inFlightEntry.second.urgent;
		});
	}

	static void RecordCompileResultLocked(const CompileJob& job, CompileResult result)
	{
		--gCompileProgress.running;
		gCompileInFlightJobs.erase(InFlightCompileJobKey(job));
		gCompileResults.push_back(std::move(result));
		gCompileCondition.notify_all();
	}

	class ScopedCompileThreadPriority
	{
#if defined(_WIN32)
		int previousPriority = THREAD_PRIORITY_NORMAL;
#endif

	  public:
		explicit ScopedCompileThreadPriority(bool urgent)
		{
#if defined(_WIN32)
			previousPriority = GetThreadPriority(GetCurrentThread());
			if (previousPriority == THREAD_PRIORITY_ERROR_RETURN)
				previousPriority = THREAD_PRIORITY_NORMAL;

			int requestedPriority = THREAD_PRIORITY_LOWEST;
			if (urgent)
				requestedPriority = THREAD_PRIORITY_BELOW_NORMAL;

			SetThreadPriority(GetCurrentThread(), requestedPriority);
#else
			(void)urgent;
#endif
		}

		~ScopedCompileThreadPriority()
		{
#if defined(_WIN32)
			SetThreadPriority(GetCurrentThread(), previousPriority);
#endif
		}
	};

	static void QueueStagedBlobCleanup(const std::string& stagedBlobPath)
	{
		if (stagedBlobPath.empty())
			return;

		QueueBackgroundCompile([stagedBlobPath]
		{
			ShaderInjectorIO::DeleteFileIfExists(stagedBlobPath);
		});
	}

	void RecordVariantUse(uint64_t shaderHash)
	{
		if (shaderHash == 0)
			return;

		gVariantUseTracker.Record(shaderHash);
	}

	uint64_t GetVariantUseStamp(uint64_t shaderHash)
	{
		return gVariantUseTracker.GetStamp(shaderHash);
	}

	static std::vector<std::pair<ModifiedShader::ModifiedShaderCompiledVariant*, uint64_t>> OrderVariantsByRecentUse(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		std::vector<ModifiedShader::ModifiedShaderCompiledVariant>& variants)
	{
		std::vector<std::pair<ModifiedShader::ModifiedShaderCompiledVariant*, uint64_t>> orderedVariants;
		orderedVariants.reserve(variants.size());
		std::unordered_map<std::string, size_t> variantIndexByDefines;
		variantIndexByDefines.reserve(variants.size());

		for (size_t variantIndex = 0; variantIndex < variants.size(); ++variantIndex)
		{
			orderedVariants.push_back({&variants[variantIndex], 0});
			variantIndexByDefines.emplace(Detail::DescribeDefines(variants[variantIndex].defines), variantIndex);
		}

		const auto updateVariantUseStamp = [&](const std::string& defineKey, uint64_t shaderHash)
		{
			const auto variantIndex = variantIndexByDefines.find(defineKey);
			if (variantIndex == variantIndexByDefines.end())
				return;

			orderedVariants[variantIndex->second].second = (std::max)(
				orderedVariants[variantIndex->second].second,
				GetVariantUseStamp(shaderHash));
		};

		for (const ModifiedShader::ModifiedShaderTargetDisk& target : modifiedShader.targets)
		{
			const std::string defineKey = Detail::DescribeDefines(target.defines);
			for (const std::string& shaderHashText : target.knownShaderBytecodeHashes)
				updateVariantUseStamp(defineKey, Hash::ParseHashText(shaderHashText));
		}

		//snapshot aliases once because a target's most recent observed hash may not be in the package record.
		{
			std::lock_guard<std::mutex> pipelineLock(HookD3D12::gPipelineMutex);
			for (const ShaderTarget::ShaderTargetDisk& shaderTarget : HookD3D12::gLoadedShaderTargets)
			{
				if (shaderTarget.modifiedShaderId != modifiedShader.id)
					continue;

				const auto* matchedVariant = modifiedShader.FindVariantForShader(shaderTarget.originalShaderBytecodeHash, shaderTarget.shaderBytecodeHashAliases);

				//render-pass variants are separate objects but use the same define sets as their source package.
				if (!matchedVariant)
					continue;

				const std::string defineKey = Detail::DescribeDefines(matchedVariant->defines);
				updateVariantUseStamp(defineKey, Hash::ParseHashText(shaderTarget.originalShaderBytecodeHash));
				for (const std::string& shaderHashText : shaderTarget.shaderBytecodeHashAliases)
					updateVariantUseStamp(defineKey, Hash::ParseHashText(shaderHashText));
			}
		}

		std::stable_sort(orderedVariants.begin(), orderedVariants.end(), [](const auto& left, const auto& right)
		{
			return left.second > right.second;
		});

		return orderedVariants;
	}

	static std::string SourceConfigurationFingerprint(
		const std::shared_ptr<const ShaderConfiguration::CompileConfiguration>& configuration,
		const std::string& sourcePath)
	{
		if (!configuration)
			return {};

		return configuration->GetSourceFingerprint(sourcePath);
	}

	static std::string ConfigurationFingerprint(const std::shared_ptr<const ShaderConfiguration::CompileConfiguration>& configuration)
	{
		if (!configuration)
			return {};

		return configuration->GetFingerprint();
	}

	static uint64_t ConfigurationOperationID(const std::shared_ptr<const ShaderConfiguration::CompileConfiguration>& configuration)
	{
		if (!configuration || !configuration->GetProgress())
			return 0;

		return configuration->GetProgress()->GetOperationID();
	}

	static bool IsConfigurationCanceled(const std::shared_ptr<const ShaderConfiguration::CompileConfiguration>& configuration)
	{
		if (!configuration || !configuration->GetProgress())
			return false;

		return configuration->GetProgress()->IsCancellationRequested();
	}

	static std::string CompileJobKey(
		const std::string& packageId,
		const std::vector<ModifiedShader::ShaderDefine>& defines,
		const std::string& configurationFingerprint,
		uint64_t operationID = 0)
	{
		return ModifiedShader::BuildCompileJobIdentity(packageId, Detail::DescribeDefines(defines), configurationFingerprint, operationID);
	}

	static std::string InFlightCompileJobKey(const CompileJob& job)
	{
		return job.key + "|" + std::to_string(job.serial);
	}

	static void CompileWorker(bool priorityOnly)
	{
		for (;;)
		{
			CompileJob job;
			BackgroundCompileTask backgroundTask;

			{
				std::unique_lock<std::mutex> lock(Detail::gCompileMutex);
				gCompileCondition.wait(lock, [priorityOnly]
				{
					if (gCompileStopRequested || (!gCompileQueue.empty() && gCompileQueue.front().urgent) || !gUrgentBackgroundCompileTasks.empty())
						return true;

					if (!ModifiedShader::CanWorkerStartNormalCompile(priorityOnly, HasUrgentWorkLocked()))
						return false;

					return !gCompileQueue.empty() || !gBackgroundCompileTasks.empty();
				});

				if (gCompileStopRequested)
					return;

				//active replacement variants run first, followed by urgent pass work, then the regular FIFO queues.
				if (!gCompileQueue.empty() && gCompileQueue.front().urgent)
				{
					job = std::move(gCompileQueue.front());
					gCompileQueue.pop_front();
					--gCompileProgress.queued;
					++gCompileProgress.running;
					gCompileInFlightJobs[InFlightCompileJobKey(job)] = job;
				}
				else if (!gUrgentBackgroundCompileTasks.empty())
				{
					backgroundTask = std::move(gUrgentBackgroundCompileTasks.front());
					gUrgentBackgroundCompileTasks.pop_front();
					++gUrgentBackgroundRunningCount;
				}
				else if (!ModifiedShader::CanWorkerStartNormalCompile(priorityOnly, HasUrgentWorkLocked()))
				{
					continue;
				}
				else if (!gCompileQueue.empty())
				{
					job = std::move(gCompileQueue.front());
					gCompileQueue.pop_front();
					--gCompileProgress.queued;
					++gCompileProgress.running;
					gCompileInFlightJobs[InFlightCompileJobKey(job)] = job;
				}
				else
				{
					backgroundTask = std::move(gBackgroundCompileTasks.front());
					gBackgroundCompileTasks.pop_front();
				}
			}

			if (backgroundTask.task)
			{
				//background tasks own their result handling and stay outside shader compile progress accounting.
				try
				{
					ScopedCompileThreadPriority threadPriority(backgroundTask.urgent);
					if (!backgroundTask.progress || !backgroundTask.progress->IsCancellationRequested())
						backgroundTask.task();
				}
				catch (const std::exception& exception)
				{
					ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseModifiedShaders->CompileWorker: background task threw an exception: " + std::string(exception.what()));
				}
				catch (...)
				{
					ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseModifiedShaders->CompileWorker: background task threw an unknown exception.");
				}

				if (backgroundTask.urgent)
				{
					std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
					--gUrgentBackgroundRunningCount;
					gCompileCondition.notify_all();
				}
				else
				{
					std::unique_lock<std::mutex> lock(Detail::gCompileMutex);
					gCompileCondition.wait_for(lock, std::chrono::milliseconds(35), []
					{
						return gCompileStopRequested || HasUrgentWorkLocked();
					});
				}

				continue;
			}

			CompileResult result;
			result.packageId = job.package->id;
			result.defines = job.defines;
			result.compiledBlobPath = job.compiledBlobPath;
			result.sourcePath = job.sourcePath;
			result.serial = job.serial;
			result.configuration = job.configuration;
			result.urgent = job.urgent;
			result.retryInterruptedOwner = job.retryInterruptedOwner;
			result.operationID = job.operationID;
			result.progressTask = std::move(job.progressTask);
			if (!IsConfigurationCanceled(job.configuration))
			{
				ScopedCompileThreadPriority threadPriority(job.urgent);
				result.succeeded = Detail::CompileVariant(*job.package, job.defines, job.candidateBasePath, result.blob, result.analysis, job.configuration);
				if (result.succeeded && !IsConfigurationCanceled(job.configuration))
				{
					result.stagedBlobPath = job.candidateBasePath + ".ready";
					result.succeeded = ShaderInjectorIO::WriteBinaryFile(result.stagedBlobPath, result.blob.data(), result.blob.size());
				}
			}

			{
				std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
				RecordCompileResultLocked(job, std::move(result));
			}

			if (!job.urgent)
			{
				std::unique_lock<std::mutex> lock(Detail::gCompileMutex);
				gCompileCondition.wait_for(lock, std::chrono::milliseconds(35), []
				{
					return gCompileStopRequested || HasUrgentWorkLocked();
				});
			}
		}
	}

	static void StartCompileWorkersLocked()
	{
		if (gCompileStopRequested || !gCompileWorkers.empty())
			return;

		const unsigned int workerCount = ModifiedShader::GetBoundedCompileWorkerCount(std::thread::hardware_concurrency());

		if (workerCount > 1)
			gCompileWorkers.emplace_back(CompileWorker, true);

		unsigned int firstRegularWorkerIndex = 0;
		if (!gCompileWorkers.empty())
			firstRegularWorkerIndex = 1;

		for (unsigned int workerIndex = firstRegularWorkerIndex; workerIndex < workerCount; ++workerIndex)
			gCompileWorkers.emplace_back(CompileWorker, false);
	}

	static std::shared_ptr<const ModifiedShader::ModifiedShaderPackageDisk> MakeCompileSnapshot(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		const std::string& ownerId,
		const std::string& sourcePath,
		const std::string& compiledBlobPath)
	{
		auto snapshot = std::make_shared<ModifiedShader::ModifiedShaderPackageDisk>();
		snapshot->id = ownerId;
		snapshot->shaderType = modifiedShader.shaderType;
		snapshot->shaderProfile = modifiedShader.shaderProfile;
		snapshot->shaderEntryPoint = modifiedShader.shaderEntryPoint;
		snapshot->sourcePath = sourcePath;
		snapshot->compiledBlobPath = compiledBlobPath;
		snapshot->compilerArguments = modifiedShader.compilerArguments;
		snapshot->targets.reserve(modifiedShader.targets.size());

		for (const ModifiedShader::ModifiedShaderTargetDisk& modifiedTarget : modifiedShader.targets)
		{
			ModifiedShader::ModifiedShaderTargetDisk targetSnapshot;
			targetSnapshot.defines = modifiedTarget.defines;
			targetSnapshot.shaderAnalysis = modifiedTarget.shaderAnalysis;
			snapshot->targets.push_back(std::move(targetSnapshot));
		}

		return snapshot;
	}

	static bool VariantNeedsCompile(
		const ModifiedShader::ModifiedShaderCompiledVariant& variant,
		bool explicitRequest,
		const std::string& configurationFingerprint)
	{
		if (explicitRequest)
			return true;

		if (variant.pendingCompileSerial != 0 && variant.pendingConfigurationFingerprint == configurationFingerprint)
			return false;

		if (variant.compileFailed && variant.failedConfigurationFingerprint == configurationFingerprint)
			return false;

		return variant.compiledBlob.empty() ||
			variant.compiledConfigurationFingerprint != configurationFingerprint ||
			!variant.compiledShaderInterfaceCompatible;
	}

	static bool QueueVariantCompileLocked(
		const std::shared_ptr<const ModifiedShader::ModifiedShaderPackageDisk>& snapshot,
		ModifiedShader::ModifiedShaderCompiledVariant& variant,
		bool urgent,
		bool explicitRequest,
		const std::shared_ptr<const ShaderConfiguration::CompileConfiguration>& configuration,
		const std::string& sourceFingerprint)
	{
		const std::string& configurationFingerprint = sourceFingerprint;
		const uint64_t operationID = ConfigurationOperationID(configuration);
		if (IsConfigurationCanceled(configuration))
			return false;

		const std::string key = CompileJobKey(snapshot->id, variant.defines, configurationFingerprint, operationID);
		const auto findQueuedJob = [&](CompileJob& queuedJob)
		{
			return queuedJob.key == key;
		};

		const auto existing = std::find_if(gCompileQueue.begin(), gCompileQueue.end(), findQueuedJob);
		const bool priorityRequest = urgent || (existing != gCompileQueue.end() && existing->urgent);
		const bool variantNeedsCompile = VariantNeedsCompile(variant, explicitRequest, configurationFingerprint);

		if (!variantNeedsCompile && (existing == gCompileQueue.end() || !priorityRequest || existing->urgent))
			return false;

		const bool sameOperationInFlight = std::any_of(gCompileInFlightJobs.begin(), gCompileInFlightJobs.end(), [&](const auto& inFlightEntry)
		{
			return inFlightEntry.second.key == key;
		});

		if (existing == gCompileQueue.end() && !explicitRequest && sameOperationInFlight)
		{
			return false;
		}

		variant.pendingCompileSerial = gNextCompileSerial++;
		variant.pendingConfigurationFingerprint = configurationFingerprint;
		variant.compileFailed = false;
		variant.failedConfigurationFingerprint.clear();
		variant.compiledBlobPath = Detail::VariantBlobPath(*snapshot, variant.defines, configuration, &sourceFingerprint);

		CompileJob job;
		job.package = snapshot;
		job.defines = variant.defines;
		job.key = key;
		job.candidateBasePath = variant.compiledBlobPath + "." + std::to_string(variant.pendingCompileSerial);
		job.compiledBlobPath = variant.compiledBlobPath;
		job.sourcePath = snapshot->sourcePath;
		job.serial = variant.pendingCompileSerial;
		job.configuration = configuration;
		job.operationID = operationID;
		job.retryInterruptedOwner = gInterruptedCompileOwners.count(snapshot->id) != 0;

		const bool existingRetryJob = existing != gCompileQueue.end() && existing->retryInterruptedOwner && existing->operationID == operationID;
		if (job.retryInterruptedOwner && !existingRetryJob)
		{
			InterruptedOwnerRetry& retry = gInterruptedOwnerRetries[snapshot->id];
			if (retry.operationID != operationID)
			{
				retry.operationID = operationID;
				retry.pendingCompileCount = 0;
				retry.failed = false;
			}
			++retry.pendingCompileCount;
		}
		std::shared_ptr<ShaderConfiguration::ConfigurationProgress> progress;
		std::shared_ptr<ShaderConfiguration::ConfigurationProgress> existingProgress;

		if (configuration)
			progress = configuration->GetProgress();

		if (existing != gCompileQueue.end() && existing->configuration)
			existingProgress = existing->configuration->GetProgress();

		if (existing != gCompileQueue.end() && existingProgress == progress)
		{
			job.progressTask = existing->progressTask;

			if (priorityRequest && !existing->urgent && job.progressTask)
				job.progressTask->PromotePriority();
		}
		else if (progress)
			//a forced apply can replace an untracked manual job with the same source fingerprint.
			job.progressTask = progress->BeginTask(priorityRequest);

		if (existing != gCompileQueue.end())
		{
			if (priorityRequest && !existing->urgent)
				++gCompileProgress.priorityTotal;

			gCompileQueue.erase(existing);
		}
		else
		{
			++gCompileProgress.queued;
			++gCompileProgress.total;
			if (priorityRequest)
				++gCompileProgress.priorityTotal;
		}

		job.urgent = priorityRequest;
		ModifiedShader::InsertCompileJobByPriority(gCompileQueue, std::move(job));

		StartCompileWorkersLocked();
		gCompileCondition.notify_all();
		return true;
	}

	void RequestVariantCompile(
		const std::string& ownerId,
		const std::vector<ModifiedShader::ShaderDefine>& defines,
		std::shared_ptr<const ShaderConfiguration::CompileConfiguration> configuration)
	{
		if (!configuration)
			configuration = DatabaseShaderConfigurations::GetAppliedConfiguration();
		std::lock_guard<std::mutex> lock(Detail::gCompileMutex);

		if (gCompileStopRequested)
			return;
		if (IsConfigurationCanceled(configuration))
			return;

		if (!gDeferredVariantRequestKeys.insert(CompileJobKey(ownerId, defines, ConfigurationFingerprint(configuration), ConfigurationOperationID(configuration))).second)
			return;

		gDeferredVariantRequests.push_back({ownerId, defines, configuration});
	}

	static ModifiedShader::ModifiedShaderCompiledVariant* FindOwnerVariant(
		const std::string& ownerId,
		const std::vector<ModifiedShader::ShaderDefine>& defines,
		ModifiedShader::ModifiedShaderPackageDisk*& outModifiedShader)
	{
		outModifiedShader = Detail::FindMutableModifiedShaderById(ownerId);

		if (outModifiedShader)
			return outModifiedShader->FindVariantForDefinesMutable(defines);

		RenderPass::RenderPassDisk* renderPass = DatabaseRenderPasses::FindRenderPassById(ownerId);

		if (!renderPass)
			return nullptr;

		const auto variant = std::find_if(renderPass->fragmentShaderVariants.begin(), renderPass->fragmentShaderVariants.end(), [&](const ModifiedShader::ModifiedShaderCompiledVariant& candidate)
		{
			return candidate.defines == defines;
		});

		if (variant == renderPass->fragmentShaderVariants.end())
			return nullptr;

		return &*variant;
	}

	static void DrainDeferredVariantRequests()
	{
		std::vector<DeferredVariantRequest> requests;

		{
			std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
			requests.swap(gDeferredVariantRequests);
			gDeferredVariantRequestKeys.clear();
		}

		for (const DeferredVariantRequest& request : requests)
		{
			if (IsConfigurationCanceled(request.configuration))
				continue;

			ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = Detail::FindMutableModifiedShaderById(request.packageId);
			RenderPass::RenderPassDisk* renderPass = nullptr;

			if (!modifiedShader)
				renderPass = DatabaseRenderPasses::FindRenderPassById(request.packageId);
			const ModifiedShader::ModifiedShaderPackageDisk* snapshotPackage = modifiedShader;
			if (renderPass)
				snapshotPackage = DatabaseRenderPasses::ResolveModifiedShader(*renderPass);

			if (!snapshotPackage)
				continue;
			ModifiedShader::ModifiedShaderPackageDisk* ownerPackage = nullptr;
			ModifiedShader::ModifiedShaderCompiledVariant* variant = FindOwnerVariant(request.packageId, request.defines, ownerPackage);

			if (!variant)
				continue;

			std::string sourcePath = snapshotPackage->sourcePath;
			std::string compiledBlobPath = snapshotPackage->compiledBlobPath;
			std::string snapshotOwnerId = snapshotPackage->id;
			if (renderPass)
			{
				RenderPass::ResolveShaderPaths(*renderPass);
				sourcePath = renderPass->fragmentShaderSourcePath;
				compiledBlobPath = renderPass->fragmentShaderCompiledBlobPath;
				snapshotOwnerId = renderPass->id;
			}
			if (SourceConfigurationFingerprint(request.configuration, sourcePath) !=
				SourceConfigurationFingerprint(DatabaseShaderConfigurations::GetAppliedConfiguration(), sourcePath))
				continue;

			const std::shared_ptr<const ModifiedShader::ModifiedShaderPackageDisk> snapshot =
				MakeCompileSnapshot(*snapshotPackage, snapshotOwnerId, sourcePath, compiledBlobPath);
			const std::string sourceFingerprint = SourceConfigurationFingerprint(request.configuration, sourcePath);
			std::lock_guard<std::mutex> lock(Detail::gCompileMutex);

			if (gCompileStopRequested)
				return;

			QueueVariantCompileLocked(snapshot, *variant, true, false, request.configuration, sourceFingerprint);
		}
	}

	int Detail::QueuePackageCompiles(ModifiedShader::ModifiedShaderPackageDisk& modifiedShader, bool includeLinkedRenderPasses)
	{
		if (modifiedShader.compiledVariants.empty())
			Detail::LoadPackageVariants(modifiedShader, false, false);

		std::vector<std::pair<ModifiedShader::ModifiedShaderCompiledVariant*, uint64_t>> orderedVariants =
			OrderVariantsByRecentUse(modifiedShader, modifiedShader.compiledVariants);
		const auto configuration = DatabaseShaderConfigurations::GetAppliedConfiguration();
		const std::string sourceFingerprint = SourceConfigurationFingerprint(configuration, modifiedShader.sourcePath);
		const std::shared_ptr<const ModifiedShader::ModifiedShaderPackageDisk> snapshot =
			MakeCompileSnapshot(modifiedShader, modifiedShader.id, modifiedShader.sourcePath, modifiedShader.compiledBlobPath);
		int queuedCount = 0;

		{
			std::lock_guard<std::mutex> lock(Detail::gCompileMutex);

			if (gCompileStopRequested)
				return 0;

			for (const auto& orderedVariant : orderedVariants)
			{
				const bool recentlyUsed = modifiedShader.enabled && orderedVariant.second != 0;
				if (QueueVariantCompileLocked(snapshot, *orderedVariant.first, recentlyUsed, true, configuration, sourceFingerprint))
					++queuedCount;
			}
		}

		ShaderInjectorGUI::WriteToRuntimeLog(
			"DatabaseModifiedShaders->CompileModifiedShader: queued " + modifiedShader.id +
			" variants=" + std::to_string(queuedCount) +
			" of=" + std::to_string(modifiedShader.compiledVariants.size()) +
			" activeFirst=1");

		if (!includeLinkedRenderPasses)
			return queuedCount;

		for (const RenderPass::RenderPassDisk& renderPass : DatabaseRenderPasses::GetRenderPasses())
		{
			if (renderPass.fragmentShaderVariants.empty() || DatabaseRenderPasses::ResolveModifiedShaderId(renderPass) != modifiedShader.id)
				continue;

			queuedCount += QueueSourceCompiles(modifiedShader, renderPass.id, renderPass.fragmentShaderSourcePath, DatabaseRenderPasses::FindRenderPassById(renderPass.id)->fragmentShaderVariants, configuration);
		}

		return queuedCount;
	}

	int QueueSourceCompiles(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		const std::string& ownerId,
		const std::string& sourcePath,
		std::vector<ModifiedShader::ModifiedShaderCompiledVariant>& variants,
		std::shared_ptr<const ShaderConfiguration::CompileConfiguration> configuration)
	{
		if (!configuration)
			configuration = DatabaseShaderConfigurations::GetAppliedConfiguration();
		std::string compiledBlobPath = modifiedShader.compiledBlobPath;
		RenderPass::RenderPassDisk* renderPass = DatabaseRenderPasses::FindRenderPassById(ownerId);
		if (renderPass)
		{
			RenderPass::ResolveShaderPaths(*renderPass);
			compiledBlobPath = renderPass->fragmentShaderCompiledBlobPath;
		}
		const std::shared_ptr<const ModifiedShader::ModifiedShaderPackageDisk> snapshot =
			MakeCompileSnapshot(modifiedShader, ownerId, sourcePath, compiledBlobPath);
		const std::string sourceFingerprint = SourceConfigurationFingerprint(configuration, sourcePath);
		int queuedCount = 0;

		std::vector<std::pair<ModifiedShader::ModifiedShaderCompiledVariant*, uint64_t>> orderedVariants =
			OrderVariantsByRecentUse(modifiedShader, variants);
		bool ownerEnabled = modifiedShader.enabled;

		if (renderPass)
			ownerEnabled = renderPass->enabled;

		std::lock_guard<std::mutex> lock(Detail::gCompileMutex);

		if (gCompileStopRequested)
			return 0;

		for (const auto& orderedVariant : orderedVariants)
		{
			const bool recentlyUsed = ownerEnabled && orderedVariant.second != 0;
			if (QueueVariantCompileLocked(snapshot, *orderedVariant.first, recentlyUsed, true, configuration, sourceFingerprint))
				++queuedCount;
		}

		return queuedCount;
	}

	void ProcessCompletedCompiles()
	{
		DrainDeferredVariantRequests();

		std::vector<CompileResult> results;
		CompileProgress progress;
		bool reportBatch = false;
		bool renderPassShaderAccepted = false;
		bool renderPassShaderUrgent = false;
		std::shared_ptr<ShaderConfiguration::ConfigurationProgressTask> renderPassPublicationTask;

		{
			std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
			if (!gCompileResults.empty())
			{
				auto selectedResult = gCompileResults.begin();
				const auto urgentResult = std::find_if(gCompileResults.begin(), gCompileResults.end(), [](const CompileResult& result)
				{
					return result.urgent;
				});

				if (urgentResult != gCompileResults.end())
					selectedResult = urgentResult;

				results.push_back(std::move(*selectedResult));
				gCompileResults.erase(selectedResult);
			}

			if (gCompileProgress.total > 0 && gCompileProgress.completed >= gCompileProgress.total &&
				gCompileQueue.empty() && gCompileProgress.running == 0 && gCompileResults.empty() && results.empty())
			{
				reportBatch = true;
				progress = gCompileProgress;
				gCompileProgress = {};
			}
		}

		if (!results.empty())
		{
			//written before gPipelineMutex so disk latency never stalls the bind hooks.
			std::vector<CompileResult*> readyResults;

			for (CompileResult& result : results)
			{
				if (IsConfigurationCanceled(result.configuration))
				{
					QueueStagedBlobCleanup(result.stagedBlobPath);
					ModifiedShader::ModifiedShaderPackageDisk* canceledOwner = nullptr;
					ModifiedShader::ModifiedShaderCompiledVariant* canceledVariant = FindOwnerVariant(result.packageId, result.defines, canceledOwner);
					if (canceledVariant)
					{
						std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
						if (canceledVariant->pendingCompileSerial == result.serial)
						{
							canceledVariant->pendingCompileSerial = 0;
							canceledVariant->pendingConfigurationFingerprint.clear();
						}
					}
					continue;
				}

				ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = nullptr;
				ModifiedShader::ModifiedShaderCompiledVariant* variant = FindOwnerVariant(result.packageId, result.defines, modifiedShader);

				if (!variant)
				{
					QueueStagedBlobCleanup(result.stagedBlobPath);
					continue;
				}

				const auto currentConfiguration = DatabaseShaderConfigurations::GetAppliedConfiguration();
				const bool configurationIsStale = SourceConfigurationFingerprint(result.configuration, result.sourcePath) !=
					SourceConfigurationFingerprint(currentConfiguration, result.sourcePath);

				if (configurationIsStale)
				{
					{
						std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
						if (variant->pendingCompileSerial == result.serial && variant->compiledBlobPath == result.compiledBlobPath)
						{
							variant->pendingCompileSerial = 0;
							variant->pendingConfigurationFingerprint.clear();
							variant->compileFailed = false;
							variant->failedConfigurationFingerprint.clear();
						}
					}

					ShaderInjectorGUI::WriteToRuntimeLogWarning("DatabaseModifiedShaders->ProcessCompletedCompiles: discarded shader result compiled with an older configuration: " + result.packageId);
					QueueStagedBlobCleanup(result.stagedBlobPath);
					continue;
				}

				bool superseded = false;
				{
					//a newer request for this variant supersedes this result.
					std::lock_guard<std::mutex> lock(Detail::gCompileMutex);

					if (variant->pendingCompileSerial != result.serial || variant->compiledBlobPath != result.compiledBlobPath)
						superseded = true;

					if (!superseded)
					{
						variant->pendingCompileSerial = 0;
						variant->pendingConfigurationFingerprint.clear();
						variant->compileFailed = !result.succeeded;
						if (!result.succeeded)
							variant->failedConfigurationFingerprint = SourceConfigurationFingerprint(result.configuration, result.sourcePath);
						else
							variant->failedConfigurationFingerprint.clear();
					}
				}

				if (superseded)
				{
					QueueStagedBlobCleanup(result.stagedBlobPath);
					continue;
				}

				if (!result.succeeded)
				{
					result.retryFailed = true;
					QueueStagedBlobCleanup(result.stagedBlobPath);
					if (result.progressTask)
						result.progressTask->MarkFailed();

					continue;
				}

				if (IsConfigurationCanceled(result.configuration))
				{
					QueueStagedBlobCleanup(result.stagedBlobPath);
					continue;
				}

				if (!ShaderInjectorIO::CommitStagedFile(result.stagedBlobPath, result.compiledBlobPath))
				{
					result.retryFailed = true;
					QueueStagedBlobCleanup(result.stagedBlobPath);
					if (result.progressTask)
						result.progressTask->MarkFailed();

					std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
					if (variant->pendingCompileSerial == 0 && variant->compiledBlobPath == result.compiledBlobPath)
					{
						variant->compileFailed = true;
						variant->failedConfigurationFingerprint = SourceConfigurationFingerprint(result.configuration, result.sourcePath);
					}

					ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseModifiedShaders->ProcessCompletedCompiles: could not write " + result.compiledBlobPath);
					continue;
				}

				readyResults.push_back(&result);
			}

			std::vector<std::string> updatedShaderTargetNames;

			if (!readyResults.empty())
			{
				std::lock_guard<std::mutex> pipelineLock(HookD3D12::gPipelineMutex);

				for (CompileResult* readyResult : readyResults)
				{
					ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = nullptr;
					ModifiedShader::ModifiedShaderCompiledVariant* variant = FindOwnerVariant(readyResult->packageId, readyResult->defines, modifiedShader);

					if (!variant)
						continue;

					if (IsConfigurationCanceled(readyResult->configuration) ||
						SourceConfigurationFingerprint(readyResult->configuration, readyResult->sourcePath) !=
						SourceConfigurationFingerprint(DatabaseShaderConfigurations::GetAppliedConfiguration(), readyResult->sourcePath))
						continue;

					{
						std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
						if (variant->pendingCompileSerial != 0 || variant->compiledBlobPath != readyResult->compiledBlobPath)
							continue;

						variant->compiledBlob = std::move(readyResult->blob);
						variant->compiledShaderAnalysis = readyResult->analysis;
						variant->compiledShaderInterfaceCompatible = true;
						variant->compiledConfigurationFingerprint = SourceConfigurationFingerprint(readyResult->configuration, readyResult->sourcePath);

						if (modifiedShader)
							Detail::UpdatePackageCompiledState(*modifiedShader);
					}

					readyResult->published = true;

					if (!modifiedShader)
					{
						renderPassShaderAccepted = true;
						renderPassShaderUrgent = renderPassShaderUrgent || readyResult->urgent;
						renderPassPublicationTask = readyResult->progressTask;
						continue;
					}

					if (readyResult->progressTask)
						readyResult->progressTask->MarkSucceeded();

					if (!modifiedShader->enabled)
						continue;

					for (int shaderTargetIndex = 0; shaderTargetIndex < static_cast<int>(HookD3D12::gLoadedShaderTargets.size()); ++shaderTargetIndex)
					{
						const ShaderTarget::ShaderTargetDisk& shaderTarget = HookD3D12::gLoadedShaderTargets[shaderTargetIndex];

						if (!HookD3D12::IsShaderTargetEffectivelyEnabled(shaderTarget))
							continue;

						if (shaderTarget.modifiedShaderId != modifiedShader->id ||
							modifiedShader->FindVariantForShader(shaderTarget.originalShaderBytecodeHash, shaderTarget.shaderBytecodeHashAliases) != variant)
							continue;

						HookD3D12::UpdateShaderTargetBlobLocked(shaderTargetIndex, variant->compiledBlob);

						if (std::find(updatedShaderTargetNames.begin(), updatedShaderTargetNames.end(), shaderTarget.name) == updatedShaderTargetNames.end())
							updatedShaderTargetNames.push_back(shaderTarget.name);
					}
				}

			}

			//formatting and logging do not need to hold the mutex used by game pipeline binds.
			if (!updatedShaderTargetNames.empty())
			{
				HookD3D12::RestartShaderTargetApplyScan();

				std::string updatedTargets;

				for (const std::string& shaderTargetName : updatedShaderTargetNames)
				{
					if (!updatedTargets.empty())
						updatedTargets += ",";

					updatedTargets += shaderTargetName;
				}

				ShaderInjectorGUI::WriteToRuntimeLog(
					"DatabaseModifiedShaders->ProcessCompletedCompiles: updated shader targets=" + std::to_string(updatedShaderTargetNames.size()) +
					" names=" + updatedTargets);
			}

		}

		if (!results.empty())
		{
			std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
			for (const CompileResult& result : results)
			{
				++gCompileProgress.completed;
				const bool canceled = IsConfigurationCanceled(result.configuration);
				if (!result.published && !canceled)
					++gCompileProgress.failed;

				if (result.urgent)
				{
					++gCompileProgress.priorityCompleted;
					if (!result.published && !canceled)
						++gCompileProgress.priorityFailed;
				}

				CompleteInterruptedOwnerRetryLocked(result);
			}
		}

		if (renderPassShaderAccepted)
			DatabaseRenderPasses::RequestRuntimeShaderPublication(renderPassShaderUrgent, std::move(renderPassPublicationTask));

		if (!reportBatch)
			return;

		const std::string summary =
			"DatabaseModifiedShaders->ProcessCompletedCompiles: background compile finished. compiled=" + std::to_string(progress.completed - progress.failed) +
			"/" + std::to_string(progress.total) + " failed=" + std::to_string(progress.failed);

		if (progress.failed == 0)
			ShaderInjectorGUI::WriteToRuntimeLogSuccess(summary);
		else
			ShaderInjectorGUI::WriteToRuntimeLogError(summary);
	}

	CompileProgress GetCompileProgress()
	{
		std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
		return gCompileProgress;
	}

	bool QueueBackgroundCompile(
		std::function<void()> compileTask,
		bool urgent,
		std::shared_ptr<ShaderConfiguration::ConfigurationProgress> progress)
	{
		if (!compileTask)
			return false;

		std::lock_guard<std::mutex> lock(Detail::gCompileMutex);

		if (gCompileStopRequested)
			return false;

		BackgroundCompileTask backgroundTask;
		backgroundTask.task = std::move(compileTask);
		backgroundTask.progress = std::move(progress);
		backgroundTask.urgent = urgent;

		if (urgent)
			gUrgentBackgroundCompileTasks.push_back(std::move(backgroundTask));
		else
			gBackgroundCompileTasks.push_back(std::move(backgroundTask));
		StartCompileWorkersLocked();
		gCompileCondition.notify_all();
		return true;
	}

	bool HasInterruptedCompiles(const std::string& ownerID)
	{
		std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
		return gInterruptedCompileOwners.count(ownerID) != 0;
	}

	void MarkInterruptedCompile(const std::string& ownerID)
	{
		if (ownerID.empty())
			return;

		std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
		gInterruptedCompileOwners.insert(ownerID);
		gInterruptedOwnerRetries.erase(ownerID);
	}

	void ClearInterruptedCompiles(const std::string& ownerID)
	{
		std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
		gInterruptedCompileOwners.erase(ownerID);
		gInterruptedOwnerRetries.erase(ownerID);
	}

	void CancelConfigurationCompiles(std::shared_ptr<ShaderConfiguration::ConfigurationProgress> progress)
	{
		if (!progress)
			return;

		progress->Cancel();
		std::vector<CompileJob> removedJobs;
		std::vector<CompileResult> removedResults;
		std::vector<BackgroundCompileTask> removedBackgroundTasks;
		const uint64_t operationID = progress->GetOperationID();

		{
			std::lock_guard<std::mutex> lock(Detail::gCompileMutex);

			for (auto job = gCompileQueue.begin(); job != gCompileQueue.end();)
			{
				if (!job->configuration || job->configuration->GetProgress() != progress)
				{
					++job;
					continue;
				}

				gInterruptedCompileOwners.insert(job->package->id);
				removedJobs.push_back(std::move(*job));
				job = gCompileQueue.erase(job);
				--gCompileProgress.queued;
				++gCompileProgress.completed;
			}

			const auto cancelBackgroundTasks = [&](std::deque<BackgroundCompileTask>& tasks)
			{
				for (auto task = tasks.begin(); task != tasks.end();)
				{
					if (task->progress != progress)
					{
						++task;
						continue;
					}

					removedBackgroundTasks.push_back(std::move(*task));
					task = tasks.erase(task);
				}
			};

			cancelBackgroundTasks(gBackgroundCompileTasks);
			cancelBackgroundTasks(gUrgentBackgroundCompileTasks);

			for (auto request = gDeferredVariantRequests.begin(); request != gDeferredVariantRequests.end();)
			{
				if (request->configuration && request->configuration->GetProgress() == progress)
				{
					gInterruptedCompileOwners.insert(request->packageId);
					request = gDeferredVariantRequests.erase(request);
				}
				else
					++request;
			}

			gDeferredVariantRequestKeys.clear();
			for (const DeferredVariantRequest& request : gDeferredVariantRequests)
				gDeferredVariantRequestKeys.insert(CompileJobKey(request.packageId, request.defines, ConfigurationFingerprint(request.configuration), ConfigurationOperationID(request.configuration)));

			for (auto result = gCompileResults.begin(); result != gCompileResults.end();)
			{
				if (result->configuration && result->configuration->GetProgress() == progress)
				{
					gInterruptedCompileOwners.insert(result->packageId);
					ModifiedShader::ModifiedShaderPackageDisk* owner = nullptr;
					ModifiedShader::ModifiedShaderCompiledVariant* variant = FindOwnerVariant(result->packageId, result->defines, owner);
					if (variant && variant->pendingCompileSerial == result->serial)
					{
						variant->pendingCompileSerial = 0;
						variant->pendingConfigurationFingerprint.clear();
					}

					++gCompileProgress.completed;
					if (result->urgent)
					{
						++gCompileProgress.priorityCompleted;
						++gCompileProgress.priorityFailed;
					}
					removedResults.push_back(std::move(*result));
					result = gCompileResults.erase(result);
				}
				else
					++result;
			}

			for (const auto& [key, job] : gCompileInFlightJobs)
			{
				if (job.configuration && job.configuration->GetProgress() == progress)
				{
					gInterruptedCompileOwners.insert(job.package->id);
					ModifiedShader::ModifiedShaderPackageDisk* owner = nullptr;
					ModifiedShader::ModifiedShaderCompiledVariant* variant = FindOwnerVariant(job.package->id, job.defines, owner);
					if (variant && variant->pendingCompileSerial == job.serial)
					{
						variant->pendingCompileSerial = 0;
						variant->pendingConfigurationFingerprint.clear();
					}
				}
			}

			for (const CompileJob& job : removedJobs)
			{
				ModifiedShader::ModifiedShaderPackageDisk* owner = nullptr;
				ModifiedShader::ModifiedShaderCompiledVariant* variant = FindOwnerVariant(job.package->id, job.defines, owner);
				if (variant && variant->pendingCompileSerial == job.serial)
				{
					variant->pendingCompileSerial = 0;
					variant->pendingConfigurationFingerprint.clear();
				}
				if (job.urgent)
				{
					++gCompileProgress.priorityCompleted;
					++gCompileProgress.priorityFailed;
				}
			}

			for (auto retry = gInterruptedOwnerRetries.begin(); retry != gInterruptedOwnerRetries.end();)
			{
				if (retry->second.operationID == operationID)
					retry = gInterruptedOwnerRetries.erase(retry);
				else
					++retry;
			}

			gCompileCondition.notify_all();
		}

		for (const CompileResult& result : removedResults)
			QueueStagedBlobCleanup(result.stagedBlobPath);
	}

	void Shutdown()
	{
		std::vector<std::thread> workers;

		{
			std::lock_guard<std::mutex> lock(Detail::gCompileMutex);

			gCompileStopRequested = true;
			gCompileQueue.clear();
			gBackgroundCompileTasks.clear();
			gUrgentBackgroundCompileTasks.clear();
			gCompileResults.clear();
			gCompileInFlightJobs.clear();
			gInterruptedOwnerRetries.clear();
			gInterruptedCompileOwners.clear();
			gDeferredVariantRequests.clear();
			gDeferredVariantRequestKeys.clear();
			gCompileProgress = {};
			workers = std::move(gCompileWorkers);
			gCompileCondition.notify_all();
		}

		for (std::thread& worker : workers)
		{
			if (worker.joinable())
				worker.join();
		}
	}
} //namespace DatabaseModifiedShaders
