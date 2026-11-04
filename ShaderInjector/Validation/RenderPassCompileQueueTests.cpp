#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "GUI/ShaderInjectorGUI.h"
#include "IO/ShaderInjectorIO.h"
#include "ModifiedShader/DatabaseModifiedShaders.h"
#include "RenderPass/DatabaseRenderPasses.h"
#include "RenderPass/RenderPass.h"
#include "RenderPass/RenderPassShaderCompileQueue.h"
#include "RenderPass/RenderPassShaders.h"
#include "ShaderConfiguration/CompileConfiguration.h"
#include "ShaderConfiguration/DatabaseShaderConfigurations.h"
#include "ShaderConfiguration/ConfigurationProgressTask.h"
#include "StringHelper.h"

namespace DatabaseRenderPasses
{
	std::vector<RenderPass::RenderPassDisk> gRenderPasses;
}

namespace
{
	std::vector<RenderPass::RenderPassDisk>& gRenderPasses = DatabaseRenderPasses::gRenderPasses;
	ModifiedShader::ModifiedShaderPackageDisk gModifiedShaderPackage;
	std::vector<std::function<void()>> gBackgroundTasks;
	std::vector<bool> gBackgroundTaskIsRenderPassCompile;
	std::unordered_map<std::string, std::vector<uint8_t>> gWrittenFiles;
	std::unordered_map<std::string, std::vector<uint8_t>> gStagedFiles;
	int gPublishCount = 0;
	bool gPublishedBeforeProgressFinished = false;
	bool gRuntimeShaderPublicationPending = false;
	int gCompileBehavior = 0;
	int gVariantCompileCount = 0;
	bool gComputeVertexPathWasEmpty = false;
	bool gBlockCompile = false;
	bool gCompilerEntered = false;
	bool gReleaseCompiler = false;
	std::mutex gCompilerMutex;
	std::condition_variable gCompilerCondition;
	bool gAcceptBackgroundTasks = true;
	bool gHasVertexSource = true;
	bool gLastLoadCachedBlobs = true;
	bool gResolveModifiedShader = false;
	bool gFailBlobWrite = false;
	int gFailureCount = 0;
	std::string gCompileConfigurationFingerprint;
	std::unordered_map<std::string, std::string> gSourceFingerprints;
	std::unordered_set<std::string> gInterruptedOwners;
	bool gLastCompileHadPreviousConfiguration = false;
	std::shared_ptr<ShaderConfiguration::ConfigurationProgress> gConfigurationProgress;
	uint8_t gConfigurationCompileCount = 0;

	void Expect(bool condition, const std::string& checkName)
	{
		if (condition)
			return;

		std::cerr << "failed: " << checkName << '\n';
		++gFailureCount;
	}

	RenderPass::RenderPassDisk MakeRenderPass(const std::string& renderPassId)
	{
		RenderPass::RenderPassDisk renderPass;
		renderPass.id = renderPassId;
		renderPass.name = renderPassId;
		renderPass.packageDirectory = "memory/" + renderPassId;
		renderPass.jsonPath = renderPass.packageDirectory + "/pass.json";
		renderPass.vertexShaderSourcePath = renderPass.packageDirectory + "/vertex.hlsl";
		renderPass.fragmentShaderSourcePath = renderPass.packageDirectory + "/fragment.hlsl";
		renderPass.vertexShaderCompiledBlobPath = renderPass.packageDirectory + "/vertex.blob";
		renderPass.fragmentShaderCompiledBlobPath = renderPass.packageDirectory + "/fragment.blob";
		renderPass.vertexShaderBlob = {1};
		renderPass.fragmentShaderBlob = {2};
		return renderPass;
	}

	void ResetFakes()
	{
		gRenderPasses.clear();
		gBackgroundTasks.clear();
		gBackgroundTaskIsRenderPassCompile.clear();
		gWrittenFiles.clear();
		gStagedFiles.clear();
		gPublishCount = 0;
		gPublishedBeforeProgressFinished = false;
		gRuntimeShaderPublicationPending = false;
		gCompileBehavior = 0;
		gVariantCompileCount = 0;
		gComputeVertexPathWasEmpty = false;
		gBlockCompile = false;
		gCompilerEntered = false;
		gReleaseCompiler = false;
		gAcceptBackgroundTasks = true;
		gHasVertexSource = true;
		gLastLoadCachedBlobs = true;
		gResolveModifiedShader = false;
		gFailBlobWrite = false;
		gCompileConfigurationFingerprint.clear();
		gSourceFingerprints.clear();
		gInterruptedOwners.clear();
		gLastCompileHadPreviousConfiguration = false;
		gConfigurationProgress.reset();
		gConfigurationCompileCount = 0;
	}

	std::thread StartNextBackgroundTaskOnThread()
	{
		Expect(!gBackgroundTasks.empty(), "queued background task exists");
		if (gBackgroundTasks.empty())
			return {};

		auto taskIndex = std::find(gBackgroundTaskIsRenderPassCompile.begin(), gBackgroundTaskIsRenderPassCompile.end(), true);
		if (taskIndex == gBackgroundTaskIsRenderPassCompile.end())
			taskIndex = gBackgroundTaskIsRenderPassCompile.begin();
		const size_t taskOffset = static_cast<size_t>(taskIndex - gBackgroundTaskIsRenderPassCompile.begin());
		std::function<void()> task = std::move(gBackgroundTasks[taskOffset]);
		gBackgroundTasks.erase(gBackgroundTasks.begin() + taskOffset);
		gBackgroundTaskIsRenderPassCompile.erase(gBackgroundTaskIsRenderPassCompile.begin() + taskOffset);
		return std::thread(std::move(task));
	}

	void RunNextBackgroundTaskOnThread()
	{
		std::thread worker = StartNextBackgroundTaskOnThread();
		if (worker.joinable())
			worker.join();
	}

	void TestQueueAndSuccessPublication()
	{
		ResetFakes();
		gRenderPasses.push_back(MakeRenderPass("success"));
		std::string error;

		Expect(DatabaseRenderPasses::QueueRenderPassShaderCompile("success", error), "enqueue succeeds");
		Expect(gBackgroundTasks.size() == 1, "enqueue returns with one stored worker task");
		const DatabaseRenderPasses::RenderPassCompileProgress queuedProgress = DatabaseRenderPasses::GetRenderPassCompileProgress();
		Expect(queuedProgress.queued == 1, "accepted task starts queued");
		Expect(queuedProgress.running == 0, "accepted task is not running before worker pickup");
		Expect(gRenderPasses.front().vertexShaderBlob == std::vector<uint8_t>{1}, "old vertex blob stays live before completion processing");
		Expect(gRenderPasses.front().fragmentShaderBlob == std::vector<uint8_t>{2}, "old fragment blob stays live before completion processing");
		Expect(!DatabaseRenderPasses::QueueRenderPassShaderCompile("success", error), "duplicate pass compile is rejected");

		gBlockCompile = true;
		std::thread worker = StartNextBackgroundTaskOnThread();
		{
			std::unique_lock<std::mutex> lock(gCompilerMutex);
			gCompilerCondition.wait(lock, [] { return gCompilerEntered; });
		}
		const DatabaseRenderPasses::RenderPassCompileProgress runningProgress = DatabaseRenderPasses::GetRenderPassCompileProgress();
		Expect(runningProgress.queued == 0, "worker pickup removes task from queued count");
		Expect(runningProgress.running == 1, "worker pickup increments running count");
		Expect(gRenderPasses.front().vertexShaderBlob == std::vector<uint8_t>{1}, "worker does not publish vertex blob");
		{
			std::lock_guard<std::mutex> lock(gCompilerMutex);
			gReleaseCompiler = true;
		}
		gCompilerCondition.notify_all();
		worker.join();
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();

		Expect(gRenderPasses.front().vertexShaderBlob == std::vector<uint8_t>{7}, "success commits new vertex blob");
		Expect(gRenderPasses.front().fragmentShaderBlob == std::vector<uint8_t>{8}, "success commits new fragment blob");
		Expect(gWrittenFiles.count(gRenderPasses.front().vertexShaderCompiledBlobPath) == 1, "accepted worker staging is atomically committed to its new versioned blob path");
		Expect(gWrittenFiles.count(gRenderPasses.front().jsonPath) == 1, "portable pass metadata is committed after its staged blobs");
		Expect(gPublishCount == 1, "successful pass publishes runtime configuration once");
	}

	void TestOneCompletionPerFrameAndUrgentFirst()
	{
		ResetFakes();
		gRenderPasses.push_back(MakeRenderPass("inactive-first"));
		gRenderPasses.front().enabled = false;
		gRenderPasses.push_back(MakeRenderPass("active-second"));
		std::string error;

		Expect(DatabaseRenderPasses::QueueRenderPassShaderCompile("inactive-first", error), "inactive pass compile queues");
		Expect(DatabaseRenderPasses::QueueRenderPassShaderCompile("active-second", error), "active pass compile queues");
		RunNextBackgroundTaskOnThread();
		RunNextBackgroundTaskOnThread();

		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();
		Expect(gRenderPasses[0].vertexShaderBlob == std::vector<uint8_t>{1}, "one frame leaves the inactive result pending");
		Expect(gRenderPasses[1].vertexShaderBlob == std::vector<uint8_t>{7}, "urgent active result is selected ahead of earlier inactive result");
		Expect(gPublishCount == 1, "active result publishes without waiting for inactive work");

		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();
		DatabaseRenderPasses::FlushPendingRuntimeShaderPublication();
		Expect(gRenderPasses[0].vertexShaderBlob == std::vector<uint8_t>{7}, "next frame commits the inactive result");
		Expect(gPublishCount == 2, "remaining result publishes after its bounded commit");
	}

	void TestTrackedRenderPassProgressWaitsForPublication()
	{
		ResetFakes();
		gRenderPasses.push_back(MakeRenderPass("tracked-progress"));
		gConfigurationProgress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		gConfigurationProgress->BeginCompiling();
		std::string error;

		Expect(DatabaseRenderPasses::QueueRenderPassShaderCompile("tracked-progress", error), "tracked pass compile enqueues");
		gConfigurationProgress->FinishDispatch();

		const ShaderConfiguration::ConfigurationProgressSnapshot queuedSnapshot = gConfigurationProgress->GetSnapshot();
		Expect(queuedSnapshot.total == 1 && queuedSnapshot.completed == 0, "tracked pass task is counted while queued");
		Expect(!queuedSnapshot.finished, "tracked progress remains open while worker task is pending");

		RunNextBackgroundTaskOnThread();
		Expect(!gConfigurationProgress->GetSnapshot().finished, "worker result retains its task token until processing");

		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();

		const ShaderConfiguration::ConfigurationProgressSnapshot completedSnapshot = gConfigurationProgress->GetSnapshot();
		Expect(completedSnapshot.finished && completedSnapshot.completed == 1, "tracked pass finishes after completion processing");
		Expect(completedSnapshot.failed == 0 && completedSnapshot.canceled == 0, "successful pass task is not failed or canceled");
		Expect(gPublishCount == 1, "tracked pass publishes before progress completes");
		Expect(gPublishedBeforeProgressFinished, "runtime publication happens before tracked task completion");
	}

	void TestComputeCompileSkipsVertexOutput()
	{
		ResetFakes();
		gRenderPasses.push_back(MakeRenderPass("compute"));
		gRenderPasses.front().executionMode = RenderPass::ExecutionMode::Compute;
		gRenderPasses.front().vertexShaderSourcePath.clear();
		std::string error;

		Expect(DatabaseRenderPasses::QueueRenderPassShaderCompile("compute", error), "compute pass enqueues");
		RunNextBackgroundTaskOnThread();
		Expect(gComputeVertexPathWasEmpty, "compute worker receives no vertex output path");
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();
		Expect(gRenderPasses.front().vertexShaderBlob == std::vector<uint8_t>{1}, "compute compile keeps vertex blob unchanged");
		Expect(gWrittenFiles.count(gRenderPasses.front().vertexShaderCompiledBlobPath) == 0, "compute compile does not create vertex blob file");
		Expect(gRenderPasses.front().fragmentShaderBlob == std::vector<uint8_t>{8}, "compute compile commits fragment-stage blob");
	}

	void TestFailedCompileKeepsOldBlobs()
	{
		ResetFakes();
		gRenderPasses.push_back(MakeRenderPass("failure"));
		gCompileBehavior = 1;
		std::string error;

		Expect(DatabaseRenderPasses::QueueRenderPassShaderCompile("failure", error), "failure scenario enqueues");
		RunNextBackgroundTaskOnThread();
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();

		Expect(gRenderPasses.front().vertexShaderBlob == std::vector<uint8_t>{1}, "failed compile preserves vertex blob");
		Expect(gRenderPasses.front().fragmentShaderBlob == std::vector<uint8_t>{2}, "failed compile preserves fragment blob");
		Expect(gPublishCount == 0, "failed compile does not publish");
	}

	void TestBlobWriteFailureKeepsOldBlobs()
	{
		ResetFakes();
		gRenderPasses.push_back(MakeRenderPass("write-failure"));
		gFailBlobWrite = true;
		std::string error;

		Expect(DatabaseRenderPasses::QueueRenderPassShaderCompile("write-failure", error), "write failure scenario enqueues");
		RunNextBackgroundTaskOnThread();
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();
		Expect(gRenderPasses.front().vertexShaderBlob == std::vector<uint8_t>{1}, "blob write failure preserves vertex blob");
		Expect(gRenderPasses.front().fragmentShaderBlob == std::vector<uint8_t>{2}, "blob write failure preserves fragment blob");
		Expect(gPublishCount == 0, "blob write failure does not publish");
	}

	void TestStaleAndDeletedResultsAreDiscarded()
	{
		ResetFakes();
		gRenderPasses.push_back(MakeRenderPass("stale-edit"));
		gWrittenFiles[gRenderPasses.front().vertexShaderCompiledBlobPath] = {1};
		std::string error;
		Expect(DatabaseRenderPasses::QueueRenderPassShaderCompile("stale-edit", error), "stale edit scenario enqueues");
		RunNextBackgroundTaskOnThread();
		gRenderPasses.front().name = "edited after enqueue";
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();
		Expect(gRenderPasses.front().vertexShaderBlob == std::vector<uint8_t>{1}, "edited configuration rejects stale result");
		Expect(gWrittenFiles[gRenderPasses.front().vertexShaderCompiledBlobPath] == std::vector<uint8_t>{1}, "stale worker staging cannot replace a live blob file");
		Expect(gPublishCount == 0, "stale edit does not publish");

		gRenderPasses.front() = MakeRenderPass("stale-refresh");
		Expect(DatabaseRenderPasses::QueueRenderPassShaderCompile("stale-refresh", error), "refresh scenario enqueues");
		RunNextBackgroundTaskOnThread();
		DatabaseRenderPasses::InvalidateRenderPassCompiles();
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();
		Expect(gRenderPasses.front().vertexShaderBlob == std::vector<uint8_t>{1}, "refresh generation rejects stale result");
		Expect(gPublishCount == 0, "refresh stale result does not publish");

		Expect(DatabaseRenderPasses::QueueRenderPassShaderCompile("stale-refresh", error), "delete scenario enqueues");
		RunNextBackgroundTaskOnThread();
		gRenderPasses.clear();
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();
		Expect(gPublishCount == 0, "deleted pass result does not publish");
	}

	void TestOldConfigurationResultDoesNotPublishAfterAtoBtoA()
	{
		ResetFakes();
		gRenderPasses.push_back(MakeRenderPass("configuration-cycle"));
		std::string error;

		gCompileConfigurationFingerprint = "A";
		Expect(DatabaseRenderPasses::QueueRenderPassShaderCompile("configuration-cycle", error), "first A configuration compile queues");
		RunNextBackgroundTaskOnThread();

		gCompileConfigurationFingerprint = "B";
		Expect(DatabaseRenderPasses::QueueRenderPassShaderCompile("configuration-cycle", error), "B configuration supersedes pending A");
		RunNextBackgroundTaskOnThread();

		gCompileConfigurationFingerprint = "A";
		Expect(DatabaseRenderPasses::QueueRenderPassShaderCompile("configuration-cycle", error), "second A configuration supersedes pending B");
		RunNextBackgroundTaskOnThread();

		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();

		Expect(gPublishCount == 1, "only the latest A compile publishes");
		Expect(gRenderPasses.front().vertexShaderBlob == std::vector<uint8_t>{63}, "older A result cannot replace latest A vertex blob");
		Expect(gRenderPasses.front().fragmentShaderBlob == std::vector<uint8_t>{73}, "older A result cannot replace latest A fragment blob");
	}

	void TestBatchProgressAndVariantCompletion()
	{
		ResetFakes();
		gRenderPasses.push_back(MakeRenderPass("batch-a"));
		gRenderPasses.push_back(MakeRenderPass("batch-b"));
		const DatabaseRenderPasses::RenderPassShaderBatchCompileResult batch = DatabaseRenderPasses::QueueAllRenderPassShaderCompiles();
		Expect(batch.compiledRenderPassCount == 2, "batch queues both passes");
		Expect(DatabaseRenderPasses::GetRenderPassCompileProgress().total == 2, "batch progress counts queued passes");
		RunNextBackgroundTaskOnThread();
		RunNextBackgroundTaskOnThread();
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();
		Expect(gPublishCount == 2, "active pass results publish as each bounded completion is committed");

		ResetFakes();
		gRenderPasses.push_back(MakeRenderPass("variants"));
		gRenderPasses.front().type = RenderPass::RenderPassType::ReplacementPixelShader;
		ModifiedShader::ModifiedShaderCompiledVariant previousVariant;
		previousVariant.defines.push_back({"VARIANT", "0"});
		previousVariant.compiledBlobPath = "memory/variants/previous-variant.blob";
		previousVariant.compiledBlob = {77};
		previousVariant.compiledConfigurationFingerprint = "previous-source-fingerprint";
		previousVariant.compiledShaderInterfaceCompatible = true;
		gRenderPasses.front().fragmentShaderVariants.push_back(previousVariant);
		std::string error;
		gVariantCompileCount = 2;
		gResolveModifiedShader = true;
		Expect(DatabaseRenderPasses::QueueRenderPassShaderCompile("variants", error), "variant scenario enqueues");
		RunNextBackgroundTaskOnThread();
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();
		Expect(!gLastLoadCachedBlobs, "accepted render pass compile skips disk variant loading");
		Expect(gRenderPasses.front().fragmentShaderVariants.front().compiledBlob == std::vector<uint8_t>{77}, "old in-memory variant stays available while new source compiles");
		Expect(gRenderPasses.front().fragmentShaderVariants.front().compiledConfigurationFingerprint == "previous-source-fingerprint", "preserved variant keeps its old source fingerprint");
		Expect(gRenderPasses.front().fragmentShaderVariants.front().compiledBlobPath != "memory/variants/previous-variant.blob", "preserved old variant does not claim the new cache path");
		const DatabaseRenderPasses::RenderPassCompileProgress variantPendingProgress = DatabaseRenderPasses::GetRenderPassCompileProgress();
		Expect(variantPendingProgress.total > 0, "variant work keeps progress pending (total=" + std::to_string(variantPendingProgress.total) + ")");
		gRenderPasses.front().fragmentShaderVariants.front().compileFailed = true;
		gRenderPasses.front().fragmentShaderVariants.front().pendingCompileSerial = 0;
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();
		const DatabaseRenderPasses::RenderPassCompileProgress failedVariantProgress = DatabaseRenderPasses::GetRenderPassCompileProgress();
		Expect(failedVariantProgress.completed == 2, "failed variant increments completed work (completed=" + std::to_string(failedVariantProgress.completed) + ")");
		Expect(failedVariantProgress.failed == 1, "failed variant increments failure count (failed=" + std::to_string(failedVariantProgress.failed) + ")");
		Expect(failedVariantProgress.queued == 1, "unfinished sibling variant remains queued (queued=" + std::to_string(failedVariantProgress.queued) + ")");

		gRenderPasses.clear();
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();
		Expect(DatabaseRenderPasses::GetRenderPassCompileProgress().total == 0, "deleted pass variants finish and discard pending work");
	}

	void TestDependencyFilteredBatch()
	{
		ResetFakes();
		gRenderPasses.push_back(MakeRenderPass("affected"));
		gRenderPasses.push_back(MakeRenderPass("unrelated"));

		for (const RenderPass::RenderPassDisk& renderPass : gRenderPasses)
		{
			gSourceFingerprints[renderPass.vertexShaderSourcePath] = "vertex";
			gSourceFingerprints[renderPass.fragmentShaderSourcePath] = "before";
		}

		const auto previousConfiguration = DatabaseShaderConfigurations::GetAppliedConfiguration();
		gCompileConfigurationFingerprint = "new-global-configuration";
		gSourceFingerprints[gRenderPasses.front().fragmentShaderSourcePath] = "after";
		const auto selectiveBatch = DatabaseRenderPasses::QueueAllRenderPassShaderCompiles(previousConfiguration);
		Expect(selectiveBatch.compiledRenderPassCount == 1 && selectiveBatch.skippedRenderPassCount == 1, "property apply only queues the pass consuming a changed property");
		Expect(gBackgroundTasks.size() == 1, "unrelated pass creates no compiler task");
		RunNextBackgroundTaskOnThread();
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();

		const auto unchangedConfiguration = DatabaseShaderConfigurations::GetAppliedConfiguration();
		const auto unchangedBatch = DatabaseRenderPasses::QueueAllRenderPassShaderCompiles(unchangedConfiguration);
		Expect(unchangedBatch.compiledRenderPassCount == 0 && unchangedBatch.skippedRenderPassCount == 2, "unchanged shader properties produce no compile work");

		const auto forcedBatch = DatabaseRenderPasses::QueueAllRenderPassShaderCompiles();
		Expect(forcedBatch.compiledRenderPassCount == 2, "force all bypasses dependency filtering");
		RunNextBackgroundTaskOnThread();
		RunNextBackgroundTaskOnThread();
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();
	}

	void TestVertexOnlyChangeSkipsFragmentVariants()
	{
		ResetFakes();
		gRenderPasses.push_back(MakeRenderPass("vertex-only"));
		gRenderPasses.front().type = RenderPass::RenderPassType::ReplacementPixelShader;
		gResolveModifiedShader = true;
		gVariantCompileCount = 2;
		gSourceFingerprints[gRenderPasses.front().vertexShaderSourcePath] = "before";
		gSourceFingerprints[gRenderPasses.front().fragmentShaderSourcePath] = "stable";
		const auto previousConfiguration = DatabaseShaderConfigurations::GetAppliedConfiguration();
		gSourceFingerprints[gRenderPasses.front().vertexShaderSourcePath] = "after";
		const auto batch = DatabaseRenderPasses::QueueAllRenderPassShaderCompiles(previousConfiguration);
		Expect(batch.compiledRenderPassCount == 1, "vertex-only property change queues its pass");
		RunNextBackgroundTaskOnThread();
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();

		for (const auto& variant : gRenderPasses.front().fragmentShaderVariants)
			Expect(variant.pendingCompileSerial == 0, "vertex-only property change leaves unchanged fragment variants unqueued");
	}

	void TestUnrelatedConfigurationChangePreservesPendingCompile()
	{
		ResetFakes();
		gRenderPasses.push_back(MakeRenderPass("stable-source"));
		gCompileConfigurationFingerprint = "before";
		gSourceFingerprints[gRenderPasses.front().vertexShaderSourcePath] = "stable-vertex";
		gSourceFingerprints[gRenderPasses.front().fragmentShaderSourcePath] = "stable-fragment";
		std::string error;
		Expect(DatabaseRenderPasses::QueueRenderPassShaderCompile("stable-source", error), "source-specific compile queues");
		RunNextBackgroundTaskOnThread();

		//only another shader's properties changed while this result was waiting to publish.
		gCompileConfigurationFingerprint = "after";
		gSourceFingerprints["another-shader.hlsl"] = "changed";
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();
		Expect(gPublishCount == 1, "unrelated global configuration edits do not discard a valid pending pass");
	}

	void TestPriorityProgressWaitsForPublication()
	{
		auto progress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		progress->BeginCompiling();
		auto priorityTask = progress->BeginTask(true);
		auto backgroundTask = progress->BeginTask();
		progress->FinishDispatch();
		priorityTask->MarkSucceeded();
		Expect(progress->GetSnapshot().priorityCompleted == 0, "compiler success is held until its publication token is released");
		priorityTask.reset();

		const auto activeReady = progress->GetSnapshot();
		Expect(activeReady.priorityTotal == 1 && activeReady.priorityCompleted == 1 && activeReady.priorityFailed == 0, "active shaders can be ready independently of background work");
		Expect(!activeReady.finished && activeReady.completed < activeReady.total, "priority completion leaves background progress running");

		backgroundTask->PromotePriority();
		backgroundTask->PromotePriority();
		Expect(progress->GetSnapshot().priorityTotal == 2, "on-demand promotion counts an existing shader once");
		backgroundTask->MarkFailed();
		backgroundTask.reset();
		Expect(progress->GetSnapshot().priorityFailed == 1, "failed active shader prevents a false ready indication");

		progress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		progress->BeginCompiling();
		priorityTask = progress->BeginTask(true);
		progress->FinishDispatch();
		priorityTask.reset();
		Expect(progress->GetSnapshot().priorityFailed == 1, "discarded active work is not reported as ready");
	}

	void TestCanceledQueuedPassRestartsWithEqualFingerprint()
	{
		ResetFakes();
		gRenderPasses.push_back(MakeRenderPass("canceled-queued"));
		gCompileConfigurationFingerprint = "same";
		gConfigurationProgress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		gConfigurationProgress->BeginCompiling();
		const auto previousConfiguration = DatabaseShaderConfigurations::GetAppliedConfiguration();
		std::string error;
		Expect(DatabaseRenderPasses::QueueRenderPassShaderCompile("canceled-queued", error), "old queued pass is registered");
		DatabaseRenderPasses::CancelConfigurationCompiles(gConfigurationProgress);
		Expect(DatabaseModifiedShaders::HasInterruptedCompiles("canceled-queued"), "canceled pass records unfinished work");

		//the fake retains the lambda to simulate a task picked up just before cancellation.
		RunNextBackgroundTaskOnThread();
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();
		Expect(gConfigurationCompileCount == 0 && gPublishCount == 0 && gWrittenFiles.empty(), "canceled queued work never compiles or publishes");

		gConfigurationProgress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		gConfigurationProgress->BeginCompiling();
		const auto retry = DatabaseRenderPasses::QueueAllRenderPassShaderCompiles(previousConfiguration);
		Expect(retry.compiledRenderPassCount == 1, "equal source fingerprints do not hide canceled work");
		RunNextBackgroundTaskOnThread();
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();
		Expect(!gLastCompileHadPreviousConfiguration, "retry rebuilds stages whose previous bytecode may still be stale");
		Expect(gPublishCount == 1 && !DatabaseModifiedShaders::HasInterruptedCompiles("canceled-queued"), "successful retry publishes and clears its interruption");
	}

	void TestCanceledRunningPassCannotPublishIntoSameConfiguration()
	{
		ResetFakes();
		gRenderPasses.push_back(MakeRenderPass("canceled-running"));
		gCompileConfigurationFingerprint = "same";
		gConfigurationProgress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		gConfigurationProgress->BeginCompiling();
		gBlockCompile = true;
		std::string error;
		Expect(DatabaseRenderPasses::QueueRenderPassShaderCompile("canceled-running", error), "old native compile is queued");
		std::thread worker = StartNextBackgroundTaskOnThread();

		{
			std::unique_lock<std::mutex> lock(gCompilerMutex);
			gCompilerCondition.wait(lock, [] { return gCompilerEntered; });
		}

		DatabaseRenderPasses::CancelConfigurationCompiles(gConfigurationProgress);
		gConfigurationProgress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		gConfigurationProgress->BeginCompiling();

		{
			std::lock_guard<std::mutex> lock(gCompilerMutex);
			gReleaseCompiler = true;
		}

		gCompilerCondition.notify_all();
		worker.join();
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();
		Expect(gPublishCount == 0 && gWrittenFiles.empty(), "old in-flight result is discarded even when its source fingerprint matches");

		gBlockCompile = false;
		const auto retry = DatabaseRenderPasses::QueueAllRenderPassShaderCompiles(DatabaseShaderConfigurations::GetAppliedConfiguration());
		Expect(retry.compiledRenderPassCount == 1, "new batch restarts the interrupted unchanged root");
		RunNextBackgroundTaskOnThread();
		DatabaseRenderPasses::ProcessCompletedRenderPassCompiles();
		Expect(gPublishCount == 1 && gRenderPasses.front().fragmentShaderBlob == std::vector<uint8_t>{72}, "only the latest native compile reaches the live pass");
		Expect(!DatabaseModifiedShaders::HasInterruptedCompiles("canceled-running"), "accepted latest result clears retry work");
	}

	void TestWorkerShutdownRejection()
	{
		ResetFakes();
		gRenderPasses.push_back(MakeRenderPass("shutdown"));
		gAcceptBackgroundTasks = false;
		std::string error;
		Expect(!DatabaseRenderPasses::QueueRenderPassShaderCompile("shutdown", error), "stopped worker rejects enqueue");
		Expect(error.find("shutting down") != std::string::npos, "shutdown rejection has an actionable error");
		Expect(gBackgroundTasks.empty(), "rejected task is not retained");
	}
}

namespace DatabaseRenderPasses
{
	void EnsureRenderPassesLoaded() {}

	std::vector<RenderPass::RenderPassDisk>& TestMutableRenderPasses()
	{
		return gRenderPasses;
	}

	const std::vector<RenderPass::RenderPassDisk>& GetRenderPasses()
	{
		return gRenderPasses;
	}

	RenderPass::RenderPassDisk* FindRenderPassById(const std::string& renderPassId)
	{
		for (RenderPass::RenderPassDisk& renderPass : gRenderPasses)
			if (renderPass.id == renderPassId)
				return &renderPass;

		return nullptr;
	}

	const ModifiedShader::ModifiedShaderPackageDisk* ResolveModifiedShader(const RenderPass::RenderPassDisk&)
	{
		if (gResolveModifiedShader)
			return &gModifiedShaderPackage;

		return nullptr;
	}

	void PublishRuntimeConfiguration(bool invalidateHistory)
	{
		++gPublishCount;

		if (gConfigurationProgress)
			gPublishedBeforeProgressFinished = !gConfigurationProgress->GetSnapshot().finished;
	}

	void RequestRuntimeShaderPublication(bool urgent, std::shared_ptr<ShaderConfiguration::ConfigurationProgressTask> progressTask)
	{
		if (urgent)
		{
			gRuntimeShaderPublicationPending = false;
			PublishRuntimeConfiguration();
			if (progressTask)
				progressTask->MarkSucceeded();
			return;
		}

		gRuntimeShaderPublicationPending = true;
		if (progressTask)
			progressTask->MarkSucceeded();
	}

	void FlushPendingRuntimeShaderPublication()
	{
		if (!gRuntimeShaderPublicationPending)
			return;

		gRuntimeShaderPublicationPending = false;
		PublishRuntimeConfiguration();
	}
}

namespace DatabaseModifiedShaders
{
	DatabaseModifiedShaders::CompileProgress GetCompileProgress()
	{
		return {};
	}

	void MarkInterruptedCompile(const std::string& ownerID)
	{
		gInterruptedOwners.insert(ownerID);
	}

	bool HasInterruptedCompiles(const std::string& ownerID)
	{
		return gInterruptedOwners.count(ownerID) != 0;
	}

	void ClearInterruptedCompiles(const std::string& ownerID)
	{
		gInterruptedOwners.erase(ownerID);
	}

	bool QueueBackgroundCompile(std::function<void()> compileTask, bool, std::shared_ptr<ShaderConfiguration::ConfigurationProgress>)
	{
		if (!gAcceptBackgroundTasks)
			return false;

		gBackgroundTasks.push_back(std::move(compileTask));
		gBackgroundTaskIsRenderPassCompile.push_back(DatabaseRenderPasses::GetRenderPassCompileProgress().queued > 0);
		return true;
	}

	int QueueSourceCompiles(const ModifiedShader::ModifiedShaderPackageDisk&, const std::string&, const std::string&, std::vector<ModifiedShader::ModifiedShaderCompiledVariant>& variants, std::shared_ptr<const ShaderConfiguration::CompileConfiguration>)
	{
		if (gVariantCompileCount == 0)
			return 0;

		for (size_t variantIndex = 0; variantIndex < variants.size() && variantIndex < static_cast<size_t>(gVariantCompileCount); ++variantIndex)
			variants[variantIndex].pendingCompileSerial = 100 + static_cast<uint64_t>(variantIndex);

		return gVariantCompileCount;
	}
}

namespace RenderPassShaders
{
	bool CompilePreparedShaders(RenderPass::RenderPassDisk& renderPass, const std::vector<std::string>&, std::string& outError, std::shared_ptr<const ShaderConfiguration::CompileConfiguration> configuration, std::shared_ptr<const ShaderConfiguration::CompileConfiguration> previousConfiguration)
	{
		gLastCompileHadPreviousConfiguration = static_cast<bool>(previousConfiguration);
		if (gBlockCompile)
		{
			std::unique_lock<std::mutex> lock(gCompilerMutex);
			gCompilerEntered = true;
			gCompilerCondition.notify_all();
			gCompilerCondition.wait(lock, [] { return gReleaseCompiler; });
		}

		if (renderPass.executionMode == RenderPass::ExecutionMode::Compute)
		{
			gComputeVertexPathWasEmpty = renderPass.vertexShaderCompiledBlobPath.empty();
			renderPass.fragmentShaderBlob = {8};
			if (!ShaderInjectorIO::WriteBinaryFile(renderPass.fragmentShaderCompiledBlobPath, renderPass.fragmentShaderBlob.data(), renderPass.fragmentShaderBlob.size()))
			{
				outError = "fake stage write failure";
				return false;
			}

			return true;
		}

		if (gCompileBehavior != 0)
		{
			outError = "fake compiler failure";
			return false;
		}

		if (configuration && !configuration->GetFingerprint().empty())
		{
			++gConfigurationCompileCount;
			renderPass.vertexShaderBlob = {static_cast<uint8_t>(60 + gConfigurationCompileCount)};
			renderPass.fragmentShaderBlob = {static_cast<uint8_t>(70 + gConfigurationCompileCount)};
		}
		else
		{
			renderPass.vertexShaderBlob = {7};
			renderPass.fragmentShaderBlob = {8};
		}

		renderPass.vertexShaderBlobHash = 70;
		renderPass.fragmentShaderBlobHash = 80;
		if ((!renderPass.vertexShaderCompiledBlobPath.empty() && !ShaderInjectorIO::WriteBinaryFile(renderPass.vertexShaderCompiledBlobPath, renderPass.vertexShaderBlob.data(), renderPass.vertexShaderBlob.size())) ||
			!ShaderInjectorIO::WriteBinaryFile(renderPass.fragmentShaderCompiledBlobPath, renderPass.fragmentShaderBlob.data(), renderPass.fragmentShaderBlob.size()))
		{
			outError = "fake stage write failure";
			return false;
		}

		return true;
	}

	void LoadShaderVariants(RenderPass::RenderPassDisk& renderPass, bool loadCachedBlobs)
	{
		gLastLoadCachedBlobs = loadCachedBlobs;

		if (gVariantCompileCount == 0)
			return;

		renderPass.fragmentShaderVariants.clear();
		for (int variantIndex = 0; variantIndex < 2; ++variantIndex)
		{
			ModifiedShader::ModifiedShaderCompiledVariant variant;
			variant.defines.push_back({"VARIANT", std::to_string(variantIndex)});
			variant.compiledBlobPath = "memory/" + renderPass.id + "/variant" + std::to_string(variantIndex) + ".blob";
			renderPass.fragmentShaderVariants.push_back(std::move(variant));
		}
	}
}

namespace RenderPass
{
	void ResolveShaderPaths(RenderPassDisk& renderPass)
	{
		if (renderPass.vertexShaderSourcePath.empty() && renderPass.executionMode != ExecutionMode::Compute)
			renderPass.vertexShaderSourcePath = "memory/vertex.hlsl";

		if (renderPass.fragmentShaderSourcePath.empty())
			renderPass.fragmentShaderSourcePath = "memory/fragment.hlsl";
	}

	bool HasShaderTemplate(const RenderPassDisk&)
	{
		return true;
	}

	ExecutionMode ResolveExecutionMode(const RenderPassDisk& renderPass)
	{
		return renderPass.executionMode;
	}

	bool IsReplacementPass(RenderPassType type)
	{
		return type == RenderPassType::ReplacementPixelShader || type == RenderPassType::ReplacementComputeShader;
	}

	void NormalizeExecutionResources(RenderPassDisk&) {}

	bool IsValidDownsampleUpsampleFactor(uint32_t)
	{
		return true;
	}

	bool WriteJson(const RenderPassDisk&)
	{
		return true;
	}
}

namespace ShaderInjectorIO
{
	static bool IsStagingPath(const std::string& filePath)
	{
		return filePath.find(".shader-compile-staging/") != std::string::npos || filePath.find(".json.compile.") != std::string::npos;
	}

	bool FileExists(const std::string& filePath)
	{
		return gHasVertexSource || filePath.find("vertex") == std::string::npos;
	}

	std::string JoinPath(const std::string& directory, const std::string& childPath)
	{
		return directory + "/" + childPath;
	}

	std::string GetInternalDirectory()
	{
		return "memory/internal";
	}

	void DirectoryCreate(const std::string&) {}

	void DeleteFileIfExists(const std::string& filePath)
	{
		gWrittenFiles.erase(filePath);
		gStagedFiles.erase(filePath);
	}

	bool WriteBinaryFile(const std::string& filePath, const void* fileData, size_t dataByteCount)
	{
		if (gFailBlobWrite && filePath.find("vertex.blob") != std::string::npos)
			return false;

		const uint8_t* bytes = static_cast<const uint8_t*>(fileData);
		const auto fileDataCopy = std::vector<uint8_t>(bytes, bytes + dataByteCount);
		if (IsStagingPath(filePath))
			gStagedFiles[filePath] = fileDataCopy;
		else
			gWrittenFiles[filePath] = fileDataCopy;
		return true;
	}

	bool WriteTextFile(const std::string& filePath, const std::string& fileText)
	{
		const std::vector<uint8_t> fileBytes(fileText.begin(), fileText.end());
		if (IsStagingPath(filePath))
			gStagedFiles[filePath] = fileBytes;
		else
			gWrittenFiles[filePath] = fileBytes;
		return true;
	}

	bool CommitStagedFile(const std::string& stagedPath, const std::string& destinationPath)
	{
		const auto stagedFile = gStagedFiles.find(stagedPath);
		if (stagedFile == gStagedFiles.end())
			return false;

		gWrittenFiles[destinationPath] = std::move(stagedFile->second);
		gStagedFiles.erase(stagedFile);
		return true;
	}

	void WriteToLogFileError(const std::string&) {}
	void WriteToLogFileWarning(const std::string&) {}
}

namespace ShaderInjectorGUI
{
	void WriteToRuntimeLog(std::string) {}
	void WriteToRuntimeLogError(std::string) {}
	void WriteToRuntimeLogSuccess(std::string) {}
	void WriteToRuntimeLogWarning(std::string) {}
}

namespace StringHelper
{
	std::string ShaderProfileForType(ShaderTarget::ShaderType shaderType)
	{
		return std::to_string(static_cast<int>(shaderType));
	}
}

namespace DatabaseShaderConfigurations
{
	std::shared_ptr<const ShaderConfiguration::CompileConfiguration> GetAppliedConfiguration()
	{
		ShaderConfiguration::DocumentDisk document;
		document.format = gCompileConfigurationFingerprint;

		for (const auto& sourceFingerprint : gSourceFingerprints)
		{
			ShaderConfiguration::PropertyDisk property;
			property.sourcePath = sourceFingerprint.first;
			property.value = sourceFingerprint.second;
			document.properties.push_back(std::move(property));
		}

		return std::make_shared<const ShaderConfiguration::CompileConfiguration>(document, gConfigurationProgress);
	}
}

namespace ShaderConfiguration
{
	CompileConfiguration::CompileConfiguration(const DocumentDisk& document, std::shared_ptr<ConfigurationProgress> progressTracker)
		: fingerprint(document.format), progress(std::move(progressTracker))
	{
		for (const ShaderConfiguration::PropertyDisk& property : document.properties)
			sourceFingerprints[property.sourcePath] = property.value;
	}

	std::string CompileConfiguration::GetSourceFingerprint(const std::string& sourcePath) const
	{
		const auto sourceFingerprint = sourceFingerprints.find(sourcePath);

		if (sourceFingerprint != sourceFingerprints.end())
			return sourceFingerprint->second;

		return fingerprint;
	}

	const std::string& CompileConfiguration::GetFingerprint() const
	{
		return fingerprint;
	}

	std::shared_ptr<ConfigurationProgress> CompileConfiguration::GetProgress() const
	{
		return progress;
	}
}

int main()
{
	TestQueueAndSuccessPublication();
	TestOneCompletionPerFrameAndUrgentFirst();
	TestTrackedRenderPassProgressWaitsForPublication();
	TestComputeCompileSkipsVertexOutput();
	TestFailedCompileKeepsOldBlobs();
	TestBlobWriteFailureKeepsOldBlobs();
	TestStaleAndDeletedResultsAreDiscarded();
	TestOldConfigurationResultDoesNotPublishAfterAtoBtoA();
	TestBatchProgressAndVariantCompletion();
	TestDependencyFilteredBatch();
	TestVertexOnlyChangeSkipsFragmentVariants();
	TestUnrelatedConfigurationChangePreservesPendingCompile();
	TestPriorityProgressWaitsForPublication();
	TestCanceledQueuedPassRestartsWithEqualFingerprint();
	TestCanceledRunningPassCannotPublishIntoSameConfiguration();
	TestWorkerShutdownRejection();

	if (gFailureCount != 0)
		return 1;

	std::cout << "Render pass compile queue tests passed.\n";
	return 0;
}
