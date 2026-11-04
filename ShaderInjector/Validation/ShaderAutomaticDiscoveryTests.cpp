#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <unordered_set>

#include "DiscoveryTestPipeline.h"
#include "Globals.h"
#include "Hash/Hash.h"
#include "HookD3D12.h"
#include "ModifiedShader/DatabaseModifiedShaders.h"
#include "ShaderDiscovery/ShaderAutomaticDiscovery.h"
#include "ShaderDiscovery/ShaderDiscovery.h"

namespace
{
	std::vector<ModifiedShader::ModifiedShaderPackageDisk> gPackages;
	std::mutex gPreparationMutex;
	std::condition_variable gPreparationCondition;
	bool gBlockPreparation = true;
	size_t gPreparationCount = 0;
	std::atomic<size_t> gPublicationCount{0};
	std::atomic<size_t> gFailedAnalysisCount{0};
	std::atomic<bool> gWrongThread{false};
	std::atomic<bool> gWrongSnapshotPointers{false};
	std::atomic<bool> gFailPreparation{false};
	std::thread::id gRenderThread;
	int gFailureCount = 0;

	void Expect(bool condition, const char* description)
	{
		if (condition)
			return;

		std::cerr << "failed: " << description << '\n';
		++gFailureCount;
	}

	bool PrepareTarget(uint64_t shaderHash, ShaderTarget::ShaderType shaderType, const ModifiedShader::ModifiedShaderPackageDisk& package, ShaderTarget::ShaderTargetDisk& target, const ShaderAnalysis::ShaderAnalysisDisk* analysis)
	{
		if (std::this_thread::get_id() == gRenderThread)
			gWrongThread = true;

		std::unique_lock<std::mutex> preparationLock(gPreparationMutex);
		++gPreparationCount;
		gPreparationCondition.notify_all();
		gPreparationCondition.wait(preparationLock, [] { return !gBlockPreparation; });

		if (analysis && !analysis->succeeded)
			++gFailedAnalysisCount;

		target.modifiedShaderId = package.id;
		target.shaderType = shaderType;
		target.originalShaderBytecodeHash = Hash::FormatHash(shaderHash);
		target.name = target.originalShaderBytecodeHash;

		return !gFailPreparation;
	}

	bool WaitForPreparation(size_t expectedCount)
	{
		std::unique_lock<std::mutex> preparationLock(gPreparationMutex);
		return gPreparationCondition.wait_for(preparationLock, std::chrono::seconds(3), [expectedCount] { return gPreparationCount >= expectedCount; });
	}

	void SetPreparationBlocked(bool blocked)
	{
		std::lock_guard<std::mutex> preparationLock(gPreparationMutex);
		gBlockPreparation = blocked;
		gPreparationCondition.notify_all();
	}

	void PumpUntilPublished(size_t expectedCount)
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

		while (gPublicationCount < expectedCount && std::chrono::steady_clock::now() < deadline)
		{
			ShaderAutomaticDiscovery::ProcessQueuedWork(65536);
			std::this_thread::yield();
		}

		Expect(gPublicationCount == expectedCount, "all queued targets eventually publish");
	}

	HookD3D12::GraphicsPipelineInfo AddGraphicsPipeline(uint64_t shaderHash)
	{
		HookD3D12::GraphicsPipelineInfo pipeline;
		pipeline.capturedPipelineLifetime.Attach(new DiscoveryTestPipeline());
		pipeline.pipelineState = pipeline.capturedPipelineLifetime.Get();
		pipeline.pixelShaderHash = shaderHash;
		pipeline.pixelShaderBytecode = {1, 2, 3, 4};
		pipeline.inputElementSemanticNames = {"POSITION"};
		pipeline.inputElements.resize(1);
		pipeline.inputElements[0].SemanticName = pipeline.inputElementSemanticNames[0].c_str();

		HookD3D12::gGraphicsPipelines.push_back(pipeline);
		return pipeline;
	}

	void AddFingerprint(uint64_t shaderHash)
	{
		ModifiedShader::ModifiedShaderTargetDisk target;
		target.knownShaderBytecodeHashes.push_back(Hash::FormatHash(shaderHash));
		target.defines.push_back({"PERMUTATION", std::to_string(shaderHash)});
		gPackages.front().targets.push_back(std::move(target));
	}
}

namespace Globals
{
	ShaderDiscoveryMode gShaderDiscoveryMode = ShaderDiscoveryMode::HashLookup;
	int gShaderDiscoveryWorkerThreads = 1;
	int gShaderDiscoveryWorkerThreadPriority = THREAD_PRIORITY_BELOW_NORMAL;
	int gShaderDiscoveryFrameJobBudget = 1;
	int gShaderDiscoveryPendingAnalysisLimit = 64;
	int gShaderDiscoveryQueuedShaderLimit = 8192;
	double gShaderDiscoveryMinimumSimilarityScore = 0.90;
	double gShaderDiscoverySimilarityAmbiguityMargin = 0.02;
}

namespace ModifiedShader
{
	bool ModifiedShaderPackageDisk::HasPerTargetDefines() const
	{
		return std::any_of(targets.begin(), targets.end(), [](const auto& target) { return !target.defines.empty(); });
	}
}

namespace DatabaseModifiedShaders
{
	const std::vector<ModifiedShader::ModifiedShaderPackageDisk>& GetModifiedShaders() { return gPackages; }

	const ModifiedShader::ModifiedShaderPackageDisk* FindModifiedShaderById(const std::string& packageID)
	{
		for (const auto& package : gPackages)
		{
			if (package.id == packageID)
				return &package;
		}

		return nullptr;
	}
}

namespace ShaderDiscovery
{
	int DiscoverEnabledModifiedShader(uint64_t, ShaderTarget::ShaderType, const std::vector<uint8_t>&, const std::vector<ModifiedShader::ModifiedShaderPackageDisk>&, ShaderAnalysis::ShaderAnalysisDisk* analysis)
	{
		//return a match with unavailable reflection to exercise forwarding a failed analysis result.
		analysis->succeeded = false;
		return 0;
	}
}

namespace StringHelper
{
	std::string ShaderTypeToString(ShaderTarget::ShaderType) { return "PixelShader"; }
}

namespace ShaderInjectorGUI
{
	void WriteToRuntimeLog(std::string) { (void)0; }

	void WriteToRuntimeLogError(std::string) { (void)0; }

	void WriteToRuntimeLogSuccess(std::string) { (void)0; }

	void WriteToRuntimeLogWarning(std::string) { (void)0; }
}

namespace HookD3D12
{
	std::mutex gPipelineMutex;
	std::vector<GraphicsPipelineInfo> gGraphicsPipelines;
	std::vector<PipelineStateInfo> gPipelineStates;
	std::vector<ShaderTarget::ShaderTargetDisk> gLoadedShaderTargets;
	bool gLoadedShaderTargetsOnce = true;

	void RefreshLoadedShaderTargets() { gLoadedShaderTargetsOnce = true; }

	void QueueShaderTargetApplyWork() { (void)0; }

	bool StreamPipelineHasShaderHash(const PipelineStateInfo& pipeline, ShaderTarget::ShaderType, uint64_t shaderHash) { return pipeline.pixelShaderHash == shaderHash; }

	void RebindPipelineStateInfoPointerFields(PipelineStateInfo&) { (void)0; }

	bool PrepareShaderTargetForPipeline(const std::string&, int, ShaderTarget::ShaderType shaderType, uint64_t shaderHash, size_t, const void*, GraphicsPipelineInfo& pipeline, const ModifiedShader::ModifiedShaderPackageDisk& package, ShaderTarget::ShaderTargetDisk& target, const ShaderAnalysis::ShaderAnalysisDisk* analysis)
	{
		if (pipeline.inputElements.front().SemanticName != pipeline.inputElementSemanticNames.front().c_str() || pipeline.originalDescription.PS.pShaderBytecode != pipeline.pixelShaderBytecode.data())
			gWrongSnapshotPointers = true;

		return PrepareTarget(shaderHash, shaderType, package, target, analysis);
	}

	bool PrepareShaderTargetForPipeline(const std::string&, int, ShaderTarget::ShaderType shaderType, uint64_t shaderHash, size_t, const void*, PipelineStateInfo&, const ModifiedShader::ModifiedShaderPackageDisk& package, ShaderTarget::ShaderTargetDisk& target, const ShaderAnalysis::ShaderAnalysisDisk* analysis)
	{
		return PrepareTarget(shaderHash, shaderType, package, target, analysis);
	}

	bool PublishPreparedShaderTarget(ShaderTarget::ShaderTargetDisk target)
	{
		if (std::this_thread::get_id() != gRenderThread)
			gWrongThread = true;

		gLoadedShaderTargets.push_back(std::move(target));
		++gPublicationCount;
		return true;
	}
}

int main(int argumentCount, char** arguments)
{
	gRenderThread = std::this_thread::get_id();
	bool singleTargetPackage = false;
	bool persistentFailure = false;
	uint64_t targetCount = 128;

	if (argumentCount > 1)
	{
		singleTargetPackage = std::string(arguments[1]) == "--single-target";
		persistentFailure = std::string(arguments[1]) == "--persistent-failure";
	}

	if (persistentFailure)
		targetCount = 1;

	ModifiedShader::ModifiedShaderPackageDisk package;
	package.id = "postprocess";
	package.shaderType = ShaderTarget::PixelShader;
	gPackages.push_back(std::move(package));

	for (uint64_t shaderHash = 1; shaderHash <= targetCount; ++shaderHash)
		AddFingerprint(shaderHash);

	if (singleTargetPackage)
	{
		for (auto& target : gPackages.front().targets)
			target.defines.clear();
	}

	ShaderAutomaticDiscovery::RefreshModifiedShaderIndex(gPackages);

	for (uint64_t shaderHash = 1; shaderHash <= targetCount; ++shaderHash)
		ShaderAutomaticDiscovery::ProcessCapturedGraphicsPipeline(AddGraphicsPipeline(shaderHash));

	if (persistentFailure)
	{
		gFailPreparation = true;
		SetPreparationBlocked(false);
		const auto retryDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3200);

		while (std::chrono::steady_clock::now() < retryDeadline)
		{
			ShaderAutomaticDiscovery::ProcessQueuedWork(65536);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		Expect(gPreparationCount == 3, "persistent failure stops after three delayed attempts");
		Expect(gPublicationCount == 0, "persistent failures never publish");
		const auto failedProgress = ShaderAutomaticDiscovery::GetProgress();
		Expect(failedProgress.failed == 1 && failedProgress.completed == failedProgress.total, "terminal failure completes progress and counts one failure");
		gFailPreparation = false;
		const auto& retryPipeline = HookD3D12::gGraphicsPipelines.front();
		ShaderAutomaticDiscovery::ProcessCapturedShader(retryPipeline, 0, ShaderTarget::PixelShader, 1, retryPipeline.pixelShaderBytecode);
		PumpUntilPublished(1);
		Expect(gPreparationCount == 4, "explicit capture can retry after automatic attempts are exhausted");
		ShaderAutomaticDiscovery::Shutdown();

		if (gFailureCount > 0)
			return 1;

		std::cout << "shader discovery persistent-failure checks passed\n";
		return 0;
	}

	if (singleTargetPackage)
	{
		SetPreparationBlocked(false);
		PumpUntilPublished(1);
		Expect(gPreparationCount == 1, "packages without permutation defines retain single-target discovery");
		ShaderAutomaticDiscovery::Shutdown();

		if (gFailureCount > 0)
			return 1;

		std::cout << "shader discovery single-target checks passed\n";
		return 0;
	}

	//a blocked disk writer must never make ProcessQueuedWork wait for it.
	const auto dispatchStart = std::chrono::steady_clock::now();
	ShaderAutomaticDiscovery::ProcessQueuedWork(65536);
	Expect(std::chrono::steady_clock::now() - dispatchStart < std::chrono::milliseconds(500), "render dispatch stays responsive while preparation is blocked");
	Expect(WaitForPreparation(1), "background target writer starts");

	for (size_t frameIndex = 0; frameIndex < 128; ++frameIndex)
		ShaderAutomaticDiscovery::ProcessQueuedWork(65536);

	Expect(gPublicationCount == 0, "a blocked target cannot publish prematurely");
	const auto blockedProgress = ShaderAutomaticDiscovery::GetProgress();
	Expect(blockedProgress.total == 128 && blockedProgress.completed == 0 && blockedProgress.created == 0, "progress includes all captured targets before preparation finishes");
	Expect(blockedProgress.running == 1 && blockedProgress.queued == 127, "progress counts queued and running work across discovery stages");
	const auto repeatedProgress = ShaderAutomaticDiscovery::GetProgress();
	Expect(repeatedProgress.total == blockedProgress.total && repeatedProgress.queued == blockedProgress.queued, "reading progress never consumes pending work");
	SetPreparationBlocked(false);
	Expect(WaitForPreparation(64), "bounded creation queue fills");

	{
		std::unique_lock<std::mutex> preparationLock(gPreparationMutex);
		const bool exceededLimit = gPreparationCondition.wait_for(preparationLock, std::chrono::milliseconds(40), [] { return gPreparationCount > 64; });
		Expect(!exceededLimit, "completed targets count toward creation backpressure");
	}

	PumpUntilPublished(128);
	const auto completedProgress = ShaderAutomaticDiscovery::GetProgress();
	Expect(completedProgress.completed == completedProgress.total && completedProgress.created == 128, "progress reaches completion after every target publishes");
	Expect(completedProgress.queued == 0 && completedProgress.running == 0 && completedProgress.failed == 0, "completed progress has no pending work or false failures");
	Expect(gPreparationCount == 128, "backpressure preserves every target exactly once");
	Expect(!gWrongThread, "only background workers prepare and only the render thread publishes");
	Expect(!gWrongSnapshotPointers, "pipeline copy points to its owned bytecode and semantic names");

	//a separate invocation covers shutdown when no analysis thread was ever started.
	if (argumentCount > 1)
	{
		ShaderAutomaticDiscovery::Shutdown();
		Expect(!ShaderAutomaticDiscovery::ProcessCapturedShader(HookD3D12::gGraphicsPipelines.front(), 0, ShaderTarget::PixelShader, 1, {1}), "direct-only shutdown rejects new work");

		if (gFailureCount > 0)
			return 1;

		std::cout << "shader discovery direct-only shutdown checks passed\n";
		return 0;
	}

	AddFingerprint(129);
	ShaderAutomaticDiscovery::RefreshModifiedShaderIndex(gPackages);
	const auto duplicatePipeline = AddGraphicsPipeline(129);
	SetPreparationBlocked(true);
	ShaderAutomaticDiscovery::ProcessCapturedShader(duplicatePipeline, 128, ShaderTarget::PixelShader, 129, duplicatePipeline.pixelShaderBytecode);
	ShaderAutomaticDiscovery::ProcessQueuedWork(65536);
	Expect(WaitForPreparation(129), "forced target begins preparation");
	ShaderAutomaticDiscovery::ProcessCapturedShader(duplicatePipeline, 128, ShaderTarget::PixelShader, 129, duplicatePipeline.pixelShaderBytecode);
	ShaderAutomaticDiscovery::ProcessQueuedWork(65536);
	SetPreparationBlocked(false);
	PumpUntilPublished(129);
	Expect(gPreparationCount == 129, "forced duplicate does not write the same target twice");

	//an index reload invalidates work prepared against the previous package snapshot.
	AddFingerprint(130);
	ShaderAutomaticDiscovery::RefreshModifiedShaderIndex(gPackages);
	SetPreparationBlocked(true);
	ShaderAutomaticDiscovery::ProcessCapturedGraphicsPipeline(AddGraphicsPipeline(130));
	ShaderAutomaticDiscovery::ProcessQueuedWork(65536);
	Expect(WaitForPreparation(130), "stale target starts preparation");
	gPackages.front().id = "postprocess-reloaded";
	ShaderAutomaticDiscovery::RefreshModifiedShaderIndex(gPackages);
	SetPreparationBlocked(false);
	PumpUntilPublished(130);
	Expect(HookD3D12::gLoadedShaderTargets.back().modifiedShaderId == "postprocess-reloaded", "stale work is rematched and only the current package publishes");
	Expect(gPreparationCount == 131, "package refresh preserves the one-shot shader capture");

	Globals::gShaderDiscoveryMode = Globals::ShaderDiscoveryMode::ShaderAnalysis;
	ShaderAutomaticDiscovery::ProcessCapturedGraphicsPipeline(AddGraphicsPipeline(131));
	PumpUntilPublished(131);
	Expect(gFailedAnalysisCount == 1, "failed analysis is forwarded to preparation without repeating it");

	//stream captures use the same worker and completion path as graphics descriptors.
	AddFingerprint(140);
	ShaderAutomaticDiscovery::RefreshModifiedShaderIndex(gPackages);
	HookD3D12::PipelineStateInfo streamPipeline;
	streamPipeline.capturedPipelineLifetime.Attach(new DiscoveryTestPipeline());
	streamPipeline.pipelineState = streamPipeline.capturedPipelineLifetime.Get();
	streamPipeline.pixelShaderHash = 140;
	streamPipeline.pixelShaderBytecode = {4, 3, 2, 1};
	HookD3D12::gPipelineStates.push_back(streamPipeline);
	ShaderAutomaticDiscovery::ProcessCapturedStreamPipeline(streamPipeline);
	PumpUntilPublished(132);

	//a one-shot capture must survive a transient write failure without another capture from the game.
	AddFingerprint(141);
	ShaderAutomaticDiscovery::RefreshModifiedShaderIndex(gPackages);
	const auto retryPipeline = AddGraphicsPipeline(141);
	gFailPreparation = true;
	ShaderAutomaticDiscovery::ProcessCapturedGraphicsPipeline(retryPipeline);
	ShaderAutomaticDiscovery::ProcessQueuedWork(65536);
	Expect(WaitForPreparation(134), "failure test starts preparation");

	const auto failureDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(60);

	while (std::chrono::steady_clock::now() < failureDeadline)
	{
		ShaderAutomaticDiscovery::ProcessQueuedWork(65536);
		std::this_thread::yield();
	}

	Expect(gPublicationCount == 132, "failed preparation never publishes a target");
	gFailPreparation = false;
	PumpUntilPublished(133);
	Expect(gPreparationCount == 135, "failed preparation automatically retries its retained capture");
	Expect(ShaderAutomaticDiscovery::GetProgress().failed == 0, "transient failures do not count as terminal progress failures");

	//shutdown has to join the creation worker even when it is still holding a live pipeline job.
	AddFingerprint(132);
	ShaderAutomaticDiscovery::RefreshModifiedShaderIndex(gPackages);
	SetPreparationBlocked(true);
	ShaderAutomaticDiscovery::ProcessCapturedGraphicsPipeline(AddGraphicsPipeline(132));
	ShaderAutomaticDiscovery::ProcessQueuedWork(65536);
	Expect(WaitForPreparation(136), "shutdown test starts an active creation");
	std::atomic<bool> shutdownReturned{false};
	std::thread shutdownThread([&shutdownReturned]
	{
		ShaderAutomaticDiscovery::Shutdown();
		shutdownReturned = true;
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	Expect(!shutdownReturned, "shutdown waits for the active creation before releasing resources");
	SetPreparationBlocked(false);
	shutdownThread.join();
	ShaderAutomaticDiscovery::ProcessQueuedWork(65536);
	Expect(gPublicationCount == 133, "shutdown clears late completions");
	Expect(!ShaderAutomaticDiscovery::ProcessCapturedShader(duplicatePipeline, 128, ShaderTarget::PixelShader, 129, duplicatePipeline.pixelShaderBytecode), "shutdown rejects new captures");

	if (gFailureCount > 0)
		return 1;

	std::cout << "shader discovery async queue checks passed\n";
	return 0;
}
