#include <iostream>
#include <stdexcept>
#include <string>

#include "../ModifiedShader/DatabaseModifiedShadersCompileQueue.cpp"

namespace
{
	ModifiedShader::ModifiedShaderPackageDisk testPackage;
	int finalBlobWriteCount = 0;
	int stagedBlobCommitCount = 0;
	std::shared_ptr<const ShaderConfiguration::CompileConfiguration> appliedConfiguration;

	void Check(bool condition, const char* message)
	{
		if (!condition)
			throw std::runtime_error(message);
	}

	std::shared_ptr<const ShaderConfiguration::CompileConfiguration> MakeConfiguration(
		const std::shared_ptr<ShaderConfiguration::ConfigurationProgress>& progress)
	{
		return std::make_shared<ShaderConfiguration::CompileConfiguration>(ShaderConfiguration::DocumentDisk{}, progress);
	}

	std::shared_ptr<const ModifiedShader::ModifiedShaderPackageDisk> MakeSnapshot(const std::string& ownerID)
	{
		auto snapshot = std::make_shared<ModifiedShader::ModifiedShaderPackageDisk>();
		snapshot->id = ownerID;
		snapshot->sourcePath = "test.hlsl";
		snapshot->compiledBlobPath = "test.cso";
		return snapshot;
	}

	void ResetQueueState()
	{
		std::lock_guard<std::mutex> lock(DatabaseModifiedShaders::Detail::gCompileMutex);
		DatabaseModifiedShaders::gCompileQueue.clear();
		DatabaseModifiedShaders::gBackgroundCompileTasks.clear();
		DatabaseModifiedShaders::gUrgentBackgroundCompileTasks.clear();
		DatabaseModifiedShaders::gCompileResults.clear();
		DatabaseModifiedShaders::gCompileInFlightJobs.clear();
		DatabaseModifiedShaders::gDeferredVariantRequests.clear();
		DatabaseModifiedShaders::gDeferredVariantRequestKeys.clear();
		DatabaseModifiedShaders::gInterruptedCompileOwners.clear();
		DatabaseModifiedShaders::gInterruptedOwnerRetries.clear();
		DatabaseModifiedShaders::gCompileProgress = {};
		DatabaseModifiedShaders::gCompileStopRequested = false;
		DatabaseModifiedShaders::gCompileWorkers.clear();
		DatabaseModifiedShaders::gCompileWorkers.emplace_back();
	}

	void TestCancellationRemovesQueuedAndDeferredWork()
	{
		ResetQueueState();
		testPackage.id = "queued-owner";
		testPackage.compiledVariants.clear();
		testPackage.compiledVariants.push_back({});
		auto snapshot = MakeSnapshot(testPackage.id);
		auto progress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		auto configuration = MakeConfiguration(progress);

		{
			std::lock_guard<std::mutex> lock(DatabaseModifiedShaders::Detail::gCompileMutex);
			Check(DatabaseModifiedShaders::QueueVariantCompileLocked(snapshot, testPackage.compiledVariants[0], false, true, configuration, "same-fingerprint"), "the initial operation should queue a variant");
			DatabaseModifiedShaders::gDeferredVariantRequests.push_back({testPackage.id, {}, configuration});
			DatabaseModifiedShaders::gDeferredVariantRequestKeys.insert("test-deferred-key");
		}

		DatabaseModifiedShaders::CancelConfigurationCompiles(progress);

		Check(DatabaseModifiedShaders::gCompileQueue.empty(), "canceling an operation should remove its queued variants");
		Check(DatabaseModifiedShaders::gDeferredVariantRequests.empty(), "canceling an operation should remove deferred requests");
		Check(testPackage.compiledVariants[0].pendingCompileSerial == 0, "canceling should clear only the matching pending serial");
		Check(DatabaseModifiedShaders::HasInterruptedCompiles(testPackage.id), "canceled queued work should mark its owner for retry");
	}

	void TestCanceledInflightJobDoesNotEraseSameFingerprintRetry()
	{
		ResetQueueState();
		testPackage.id = "inflight-owner";
		testPackage.compiledVariants.clear();
		testPackage.compiledVariants.push_back({});
		auto snapshot = MakeSnapshot(testPackage.id);
		auto oldProgress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		auto oldConfiguration = MakeConfiguration(oldProgress);
		std::string oldInFlightKey;

		{
			std::lock_guard<std::mutex> lock(DatabaseModifiedShaders::Detail::gCompileMutex);
			DatabaseModifiedShaders::QueueVariantCompileLocked(snapshot, testPackage.compiledVariants[0], false, true, oldConfiguration, "same-fingerprint");
			DatabaseModifiedShaders::CompileJob oldJob = std::move(DatabaseModifiedShaders::gCompileQueue.front());
			DatabaseModifiedShaders::gCompileQueue.pop_front();
			oldInFlightKey = DatabaseModifiedShaders::InFlightCompileJobKey(oldJob);
			DatabaseModifiedShaders::gCompileInFlightJobs.emplace(oldInFlightKey, oldJob);
			--DatabaseModifiedShaders::gCompileProgress.queued;
			++DatabaseModifiedShaders::gCompileProgress.running;
		}

		DatabaseModifiedShaders::CancelConfigurationCompiles(oldProgress);
		auto newProgress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		auto newConfiguration = MakeConfiguration(newProgress);

		{
			std::lock_guard<std::mutex> lock(DatabaseModifiedShaders::Detail::gCompileMutex);
			DatabaseModifiedShaders::QueueVariantCompileLocked(snapshot, testPackage.compiledVariants[0], false, true, newConfiguration, "same-fingerprint");
			DatabaseModifiedShaders::CompileJob newJob = std::move(DatabaseModifiedShaders::gCompileQueue.front());
			DatabaseModifiedShaders::gCompileQueue.pop_front();
			const std::string newInFlightKey = DatabaseModifiedShaders::InFlightCompileJobKey(newJob);
			DatabaseModifiedShaders::gCompileInFlightJobs.emplace(newInFlightKey, newJob);

			Check(oldInFlightKey != newInFlightKey, "same-fingerprint work from a later operation must have a distinct inflight identity");
			DatabaseModifiedShaders::gCompileInFlightJobs.erase(oldInFlightKey);
			Check(DatabaseModifiedShaders::gCompileInFlightJobs.count(newInFlightKey) == 1, "finishing canceled work must not erase a newer same-fingerprint job");
		}

		const uint64_t newPendingSerial = testPackage.compiledVariants[0].pendingCompileSerial;
		DatabaseModifiedShaders::CompileResult canceledResult;
		canceledResult.packageId = testPackage.id;
		canceledResult.defines = testPackage.compiledVariants[0].defines;
		canceledResult.compiledBlobPath = testPackage.compiledVariants[0].compiledBlobPath;
		canceledResult.serial = newPendingSerial - 1;
		canceledResult.succeeded = true;
		canceledResult.configuration = oldConfiguration;
		canceledResult.blob = {1, 2, 3};
		testPackage.compiledVariants[0].compiledBlob = {9};
		{
			std::lock_guard<std::mutex> lock(DatabaseModifiedShaders::Detail::gCompileMutex);
			DatabaseModifiedShaders::gCompileResults.push_back(std::move(canceledResult));
		}

		DatabaseModifiedShaders::ProcessCompletedCompiles();

		Check(finalBlobWriteCount == 0, "a canceled in-flight result must not write over the same-fingerprint retry");
		Check(testPackage.compiledVariants[0].compiledBlob == std::vector<uint8_t>{9}, "a canceled in-flight result must not publish its blob");
		Check(testPackage.compiledVariants[0].pendingCompileSerial == newPendingSerial, "discarding old work must preserve the new pending serial");
	}

	void TestCancellationClearsPendingSerialForBufferedResult()
	{
		ResetQueueState();
		testPackage.id = "buffered-owner";
		testPackage.compiledVariants.clear();
		testPackage.compiledVariants.push_back({});
		auto snapshot = MakeSnapshot(testPackage.id);
		auto progress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		auto configuration = MakeConfiguration(progress);
		uint64_t pendingSerial = 0;

		{
			std::lock_guard<std::mutex> lock(DatabaseModifiedShaders::Detail::gCompileMutex);
			DatabaseModifiedShaders::QueueVariantCompileLocked(snapshot, testPackage.compiledVariants[0], false, true, configuration, "buffered-fingerprint");
			DatabaseModifiedShaders::CompileJob completedJob = std::move(DatabaseModifiedShaders::gCompileQueue.front());
			DatabaseModifiedShaders::gCompileQueue.pop_front();
			--DatabaseModifiedShaders::gCompileProgress.queued;
			pendingSerial = completedJob.serial;

			DatabaseModifiedShaders::CompileResult completedResult;
			completedResult.packageId = completedJob.package->id;
			completedResult.defines = completedJob.defines;
			completedResult.serial = completedJob.serial;
			completedResult.compiledBlobPath = completedJob.compiledBlobPath;
			completedResult.configuration = configuration;
			DatabaseModifiedShaders::gCompileResults.push_back(std::move(completedResult));
		}

		Check(testPackage.compiledVariants[0].pendingCompileSerial == pendingSerial, "completed but unpublished work should retain its pending serial before cancellation");
		DatabaseModifiedShaders::CancelConfigurationCompiles(progress);

		Check(DatabaseModifiedShaders::gCompileResults.empty(), "canceling should discard buffered unpublished results");
		Check(testPackage.compiledVariants[0].pendingCompileSerial == 0, "canceling a buffered result should clear its matching pending serial");
		Check(testPackage.compiledVariants[0].pendingConfigurationFingerprint.empty(), "canceling a buffered result should clear its pending fingerprint");
	}

	void TestInterruptedOwnerClearsOnlyAfterEverySuccessfulRetry()
	{
		ResetQueueState();
		const std::string ownerID = "retry-owner";
		DatabaseModifiedShaders::MarkInterruptedCompile(ownerID);
		auto progress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		const uint64_t operationID = progress->GetOperationID();

		{
			std::lock_guard<std::mutex> lock(DatabaseModifiedShaders::Detail::gCompileMutex);
			DatabaseModifiedShaders::gInterruptedOwnerRetries[ownerID] = {operationID, 2, false};
			DatabaseModifiedShaders::CompileResult normalResult;
			normalResult.packageId = ownerID;
			normalResult.operationID = operationID;
			normalResult.retryInterruptedOwner = true;
			normalResult.published = true;
			DatabaseModifiedShaders::CompleteInterruptedOwnerRetryLocked(normalResult);
		}

		Check(DatabaseModifiedShaders::HasInterruptedCompiles(ownerID), "one successful inactive retry must not clear an owner with remaining work");

		{
			std::lock_guard<std::mutex> lock(DatabaseModifiedShaders::Detail::gCompileMutex);
			DatabaseModifiedShaders::CompileResult activeResult;
			activeResult.packageId = ownerID;
			activeResult.operationID = operationID;
			activeResult.retryInterruptedOwner = true;
			activeResult.published = true;
			activeResult.urgent = true;
			DatabaseModifiedShaders::CompleteInterruptedOwnerRetryLocked(activeResult);
		}

		Check(!DatabaseModifiedShaders::HasInterruptedCompiles(ownerID), "all successful active and inactive retries should clear the owner");

		DatabaseModifiedShaders::MarkInterruptedCompile(ownerID);
		{
			std::lock_guard<std::mutex> lock(DatabaseModifiedShaders::Detail::gCompileMutex);
			DatabaseModifiedShaders::gInterruptedOwnerRetries[ownerID] = {operationID, 1, false};
			DatabaseModifiedShaders::CompileResult failedResult;
			failedResult.packageId = ownerID;
			failedResult.operationID = operationID;
			failedResult.retryInterruptedOwner = true;
			failedResult.retryFailed = true;
			DatabaseModifiedShaders::CompleteInterruptedOwnerRetryLocked(failedResult);
		}

		Check(DatabaseModifiedShaders::HasInterruptedCompiles(ownerID), "a failed retry must keep the owner marked for another apply");
	}

	void TestCompletionDrainHandlesUrgentFirstAndOneResultPerCall()
	{
		ResetQueueState();
		auto normalProgress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		auto urgentProgress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		auto normalConfiguration = MakeConfiguration(normalProgress);
		auto urgentConfiguration = MakeConfiguration(urgentProgress);
		normalProgress->Cancel();
		urgentProgress->Cancel();

		DatabaseModifiedShaders::CompileResult normalResult;
		normalResult.packageId = "normal-result";
		normalResult.configuration = normalConfiguration;
		DatabaseModifiedShaders::CompileResult urgentResult;
		urgentResult.packageId = "urgent-result";
		urgentResult.urgent = true;
		urgentResult.configuration = urgentConfiguration;

		{
			std::lock_guard<std::mutex> lock(DatabaseModifiedShaders::Detail::gCompileMutex);
			DatabaseModifiedShaders::gCompileProgress.total = 2;
			DatabaseModifiedShaders::gCompileResults.push_back(std::move(normalResult));
			DatabaseModifiedShaders::gCompileResults.push_back(std::move(urgentResult));
		}

		DatabaseModifiedShaders::ProcessCompletedCompiles();

		Check(DatabaseModifiedShaders::gCompileResults.size() == 1, "completion should process at most one buffered result per call");
		Check(DatabaseModifiedShaders::gCompileProgress.priorityCompleted == 1, "urgent results should be selected before earlier normal results");

		DatabaseModifiedShaders::ProcessCompletedCompiles();

		Check(DatabaseModifiedShaders::gCompileResults.empty(), "the next call should drain the remaining result");
		Check(DatabaseModifiedShaders::gCompileProgress.completed == 2, "completion accounting should include each result after processing");
	}

	void TestSuccessfulBufferedResultCommitsStageAndPublishesBlob()
	{
		ResetQueueState();
		testPackage.id = "staged-owner";
		testPackage.compiledVariants.clear();
		testPackage.compiledVariants.push_back({});
		testPackage.compiledVariants[0].pendingCompileSerial = 79;
		testPackage.compiledVariants[0].compiledBlobPath = "staged-owner.cso";
		auto progress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		auto configuration = MakeConfiguration(progress);
		appliedConfiguration = configuration;

		DatabaseModifiedShaders::CompileResult result;
		result.packageId = testPackage.id;
		result.compiledBlobPath = testPackage.compiledVariants[0].compiledBlobPath;
		result.stagedBlobPath = "staged-owner.ready";
		result.serial = 79;
		result.succeeded = true;
		result.configuration = configuration;
		result.blob = {4, 5, 6};
		{
			std::lock_guard<std::mutex> lock(DatabaseModifiedShaders::Detail::gCompileMutex);
			DatabaseModifiedShaders::gCompileResults.push_back(std::move(result));
		}

		DatabaseModifiedShaders::ProcessCompletedCompiles();

		Check(stagedBlobCommitCount == 1, "frame completion should atomically commit the staged blob");
		Check(finalBlobWriteCount == 0, "frame completion should not write compiled payload bytes");
		Check(testPackage.compiledVariants[0].compiledBlob == std::vector<uint8_t>({4, 5, 6}), "successful staged output should become the published variant blob");
		appliedConfiguration.reset();
	}

	void TestGeneralWorkerWakesAfterLastUrgentCompileFinishes()
	{
		ResetQueueState();
		auto package = MakeSnapshot("urgent-owner");
		DatabaseModifiedShaders::CompileJob firstUrgentJob;
		firstUrgentJob.package = package;
		firstUrgentJob.key = "urgent-first";
		firstUrgentJob.serial = 1;
		firstUrgentJob.urgent = true;
		DatabaseModifiedShaders::CompileJob lastUrgentJob;
		lastUrgentJob.package = package;
		lastUrgentJob.key = "urgent-last";
		lastUrgentJob.serial = 2;
		lastUrgentJob.urgent = true;
		DatabaseModifiedShaders::CompileJob normalJob;
		normalJob.package = package;
		normalJob.key = "normal-next";
		normalJob.serial = 3;
		DatabaseModifiedShaders::gCompileQueue.push_back(normalJob);
		DatabaseModifiedShaders::gCompileInFlightJobs.emplace(DatabaseModifiedShaders::InFlightCompileJobKey(firstUrgentJob), firstUrgentJob);
		DatabaseModifiedShaders::gCompileInFlightJobs.emplace(DatabaseModifiedShaders::InFlightCompileJobKey(lastUrgentJob), lastUrgentJob);
		DatabaseModifiedShaders::gCompileProgress.running = 2;

		bool normalWorkerWoke = false;
		std::atomic<bool> normalWorkerWaiting = false;
		std::thread normalWorker([&]
		{
			std::unique_lock<std::mutex> lock(DatabaseModifiedShaders::Detail::gCompileMutex);
			normalWorkerWaiting.store(true, std::memory_order_release);
			normalWorkerWoke = DatabaseModifiedShaders::gCompileCondition.wait_for(lock, std::chrono::seconds(2), []
			{
				return ModifiedShader::CanWorkerStartNormalCompile(false, DatabaseModifiedShaders::HasUrgentWorkLocked()) &&
					!DatabaseModifiedShaders::gCompileQueue.empty();
			});
		});
		while (!normalWorkerWaiting.load(std::memory_order_acquire))
			std::this_thread::yield();

		{
			std::lock_guard<std::mutex> lock(DatabaseModifiedShaders::Detail::gCompileMutex);
			DatabaseModifiedShaders::CompileResult firstResult;
			DatabaseModifiedShaders::RecordCompileResultLocked(firstUrgentJob, std::move(firstResult));
		}

		{
			std::lock_guard<std::mutex> lock(DatabaseModifiedShaders::Detail::gCompileMutex);
			Check(!ModifiedShader::CanWorkerStartNormalCompile(false, DatabaseModifiedShaders::HasUrgentWorkLocked()), "normal work must remain blocked while another urgent compile is inflight");
			DatabaseModifiedShaders::CompileResult lastResult;
			DatabaseModifiedShaders::RecordCompileResultLocked(lastUrgentJob, std::move(lastResult));
		}

		normalWorker.join();
		Check(normalWorkerWoke, "the final urgent completion must wake the general worker so normal work resumes");
	}
}

namespace ShaderInjectorIO
{
	std::string GetModifiedShadersDirectory()
	{
		return ".";
	}

	bool WriteBinaryFile(const std::string&, const void*, size_t)
	{
		++finalBlobWriteCount;
		return true;
	}

	bool CommitStagedFile(const std::string&, const std::string&)
	{
		++stagedBlobCommitCount;
		return true;
	}

	void DeleteFileIfExists(const std::string&)
	{
	}
}

namespace DatabaseModifiedShaders::Detail
{
	ModifiedShader::ModifiedShaderPackageDisk* FindMutableModifiedShaderById(const std::string& ownerID)
	{
		if (testPackage.id == ownerID)
			return &testPackage;

		return nullptr;
	}

	std::string DescribeDefines(const std::vector<ModifiedShader::ShaderDefine>&)
	{
		return {};
	}

	std::string VariantBlobPath(
		const ModifiedShader::ModifiedShaderPackageDisk& package,
		const std::vector<ModifiedShader::ShaderDefine>&,
		const std::shared_ptr<const ShaderConfiguration::CompileConfiguration>&,
		const std::string*)
	{
		return package.compiledBlobPath;
	}

	void UpdatePackageCompiledState(ModifiedShader::ModifiedShaderPackageDisk&)
	{
	}

	void LoadPackageVariants(ModifiedShader::ModifiedShaderPackageDisk&, bool, bool)
	{
	}

	bool CompileVariant(
		const ModifiedShader::ModifiedShaderPackageDisk&,
		const std::vector<ModifiedShader::ShaderDefine>&,
		const std::string&,
		std::vector<uint8_t>&,
		ShaderAnalysis::ShaderAnalysisDisk&,
		const std::shared_ptr<const ShaderConfiguration::CompileConfiguration>&)
	{
		return false;
	}
}

namespace RenderPass
{
	void ResolveShaderPaths(RenderPassDisk&)
	{
	}
}

namespace DatabaseRenderPasses
{
	void RequestRuntimeShaderPublication(
		bool,
		std::shared_ptr<ShaderConfiguration::ConfigurationProgressTask> progressTask)
	{
		if (progressTask)
			progressTask->MarkSucceeded();
	}

	const std::vector<RenderPass::RenderPassDisk>& GetRenderPasses()
	{
		static const std::vector<RenderPass::RenderPassDisk> renderPasses;
		return renderPasses;
	}

	RenderPass::RenderPassDisk* FindRenderPassById(const std::string&)
	{
		return nullptr;
	}

	std::string ResolveModifiedShaderId(const RenderPass::RenderPassDisk&)
	{
		return {};
	}

	const ModifiedShader::ModifiedShaderPackageDisk* ResolveModifiedShader(const RenderPass::RenderPassDisk&)
	{
		return nullptr;
	}

	void PublishRuntimeConfiguration(bool)
	{
	}
}

namespace DatabaseShaderConfigurations
{
	std::shared_ptr<const ShaderConfiguration::CompileConfiguration> GetAppliedConfiguration()
	{
		return appliedConfiguration;
	}
}

namespace HookD3D12
{
	std::mutex gPipelineMutex;
	std::vector<ShaderTarget::ShaderTargetDisk> gLoadedShaderTargets;

	void RestartShaderTargetApplyScan()
	{
	}

	void UpdateShaderTargetBlobLocked(int, const std::vector<uint8_t>&)
	{
	}

	bool IsShaderTargetEffectivelyEnabled(const ShaderTarget::ShaderTargetDisk&)
	{
		return false;
	}
}

namespace ModifiedShader
{
	ModifiedShaderCompiledVariant* ModifiedShaderPackageDisk::FindVariantForDefinesMutable(const std::vector<ShaderDefine>& defines)
	{
		for (ModifiedShaderCompiledVariant& variant : compiledVariants)
			if (variant.defines == defines)
				return &variant;

		return nullptr;
	}

	const ModifiedShaderCompiledVariant* ModifiedShaderPackageDisk::FindVariantForShader(const std::string&, const std::vector<std::string>&) const
	{
		return nullptr;
	}
}

namespace Hash
{
	uint64_t ParseHashText(const std::string&)
	{
		return 0;
	}
}

namespace ShaderInjectorGUI
{
	void WriteToRuntimeLog(std::string)
	{
	}

	void WriteToRuntimeLogError(std::string)
	{
	}

	void WriteToRuntimeLogSuccess(std::string)
	{
	}

	void WriteToRuntimeLogWarning(std::string)
	{
	}
}

namespace ShaderConfiguration
{
	CompileConfiguration::CompileConfiguration(const DocumentDisk&, std::shared_ptr<ConfigurationProgress> progressTracker)
		: progress(std::move(progressTracker))
	{
	}

	const std::vector<std::string>& CompileConfiguration::GetDefines() const
	{
		static const std::vector<std::string> emptyDefines;
		return emptyDefines;
	}

	const std::string& CompileConfiguration::GetFingerprint() const
	{
		static const std::string fingerprint = "test-fingerprint";
		return fingerprint;
	}

	std::string CompileConfiguration::GetSourceFingerprint(const std::string&) const
	{
		return "test-fingerprint";
	}

	std::shared_ptr<ConfigurationProgress> CompileConfiguration::GetProgress() const
	{
		return progress;
	}

	bool TryGetBoolean(const PropertyDisk&, bool&)
	{
		return false;
	}

	bool TryGetInteger(const PropertyDisk&, int&)
	{
		return false;
	}

	bool TryGetFloatComponents(const PropertyDisk&, std::vector<float>&)
	{
		return false;
	}

	size_t ComponentCount(const PropertyDisk&)
	{
		return 0;
	}
}

int main()
{
	try
	{
		TestCancellationRemovesQueuedAndDeferredWork();
		TestCanceledInflightJobDoesNotEraseSameFingerprintRetry();
		TestCancellationClearsPendingSerialForBufferedResult();
		TestInterruptedOwnerClearsOnlyAfterEverySuccessfulRetry();
		TestCompletionDrainHandlesUrgentFirstAndOneResultPerCall();
		TestSuccessfulBufferedResultCommitsStageAndPublishesBlob();
		TestGeneralWorkerWakesAfterLastUrgentCompileFinishes();
		std::cout << "Modified shader compile lifecycle tests passed.\n";
		return 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what() << '\n';
		return 1;
	}
}
