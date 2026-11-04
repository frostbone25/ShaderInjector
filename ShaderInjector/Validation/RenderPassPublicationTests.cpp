#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "IO/ShaderInjectorIO.h"
#include "ModifiedShader/DatabaseModifiedShaders.h"
#include "RenderPass/DatabaseRenderPasses.h"
#include "RenderPass/RenderPassConfigurationProperties.h"
#include "RenderPass/RenderPassRuntime.h"
#include "RenderPass/RenderPassShaderCompileQueue.h"
#include "RenderPass/RenderPassTexturePool.h"
#include "ShaderConfiguration/ConfigurationProgress.h"
#include "ShaderConfiguration/ConfigurationProgressTask.h"
#include "ShaderConfiguration/DatabaseShaderConfigurations.h"

namespace
{
	DatabaseModifiedShaders::CompileProgress gModifiedShaderProgress;
	DatabaseRenderPasses::RenderPassCompileProgress gRenderPassProgress;
	int gPublicationCount = 0;
	int gFailureCount = 0;
	bool gConfigurationApplySucceeds = true;

	void Expect(bool condition, const std::string& label)
	{
		if (condition)
			return;

		std::cerr << "failed: " << label << '\n';
		++gFailureCount;
	}

	void Reset()
	{
		DatabaseRenderPasses::gRenderPasses.clear();
		gModifiedShaderProgress = {};
		gRenderPassProgress = {};
		gPublicationCount = 0;
		gConfigurationApplySucceeds = true;
	}

	std::shared_ptr<ShaderConfiguration::ConfigurationProgress> MakeProgress(
		std::shared_ptr<ShaderConfiguration::ConfigurationProgressTask>& firstTask,
		std::shared_ptr<ShaderConfiguration::ConfigurationProgressTask>& secondTask)
	{
		auto progress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		progress->BeginCompiling();
		firstTask = progress->BeginTask(true);
		secondTask = progress->BeginTask(true);
		progress->FinishDispatch();
		return progress;
	}

	void TestInactiveRequestsCoalesceAndHoldTasksUntilFlush()
	{
		Reset();
		std::shared_ptr<ShaderConfiguration::ConfigurationProgressTask> firstTask;
		std::shared_ptr<ShaderConfiguration::ConfigurationProgressTask> secondTask;
		auto progress = MakeProgress(firstTask, secondTask);
		DatabaseRenderPasses::RequestRuntimeShaderPublication(false, firstTask);
		DatabaseRenderPasses::RequestRuntimeShaderPublication(false, secondTask);
		firstTask.reset();
		secondTask.reset();

		Expect(progress->GetSnapshot().priorityCompleted == 0, "inactive publication keeps priority task tokens pending");
		DatabaseRenderPasses::FlushPendingRuntimeShaderPublication();
		Expect(gPublicationCount == 1, "drained inactive requests coalesce into one runtime publication");
		Expect(progress->GetSnapshot().priorityCompleted == 2, "publication releases all retained progress tokens");
	}

	void TestInactivePublicationWaitsBrieflyWhileWorkContinues()
	{
		Reset();
		gModifiedShaderProgress.queued = 1;
		auto progress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		progress->BeginCompiling();
		auto firstTask = progress->BeginTask(true);
		progress->FinishDispatch();
		DatabaseRenderPasses::RequestRuntimeShaderPublication(false, firstTask);
		firstTask.reset();

		DatabaseRenderPasses::FlushPendingRuntimeShaderPublication();
		Expect(gPublicationCount == 0, "inactive publication waits while compile work remains");
		Expect(progress->GetSnapshot().priorityCompleted == 0, "waiting publication does not report priority work complete");

		std::this_thread::sleep_for(std::chrono::milliseconds(220));
		DatabaseRenderPasses::FlushPendingRuntimeShaderPublication();
		Expect(gPublicationCount == 1, "inactive publication flushes after the bounded delay");
		Expect(progress->GetSnapshot().priorityCompleted == 1, "delayed publication releases its retained task");
	}

	void TestUrgentPublicationBypassesDelay()
	{
		Reset();
		gModifiedShaderProgress.queued = 1;
		auto progress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		progress->BeginCompiling();
		auto firstTask = progress->BeginTask(true);
		progress->FinishDispatch();
		DatabaseRenderPasses::RequestRuntimeShaderPublication(true, firstTask);
		firstTask.reset();

		DatabaseRenderPasses::FlushPendingRuntimeShaderPublication();
		Expect(gPublicationCount == 1, "urgent publication bypasses pending inactive compile work");
		Expect(progress->GetSnapshot().priorityCompleted == 1, "urgent publication releases its retained task");
	}

	void TestFailedRuntimePublicationFailsHeldTask()
	{
		Reset();
		gConfigurationApplySucceeds = false;
		auto progress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		progress->BeginCompiling();
		auto task = progress->BeginTask(true);
		progress->FinishDispatch();
		DatabaseRenderPasses::RequestRuntimeShaderPublication(true, task);
		task.reset();
		DatabaseRenderPasses::FlushPendingRuntimeShaderPublication();

		const auto snapshot = progress->GetSnapshot();
		Expect(gPublicationCount == 0, "failed configuration validation does not publish a runtime snapshot");
		Expect(snapshot.priorityCompleted == 1 && snapshot.priorityFailed == 1, "failed runtime publication fails the held priority task");
	}
}

namespace DatabaseModifiedShaders
{
	CompileProgress GetCompileProgress()
	{
		return gModifiedShaderProgress;
	}
}

namespace DatabaseRenderPasses
{
	std::vector<RenderPass::RenderPassDisk> gRenderPasses;

	RenderPassCompileProgress GetRenderPassCompileProgress()
	{
		return gRenderPassProgress;
	}
}

namespace DatabaseShaderConfigurations
{
	ShaderConfiguration::DocumentDisk GetAppliedDocument()
	{
		return {};
	}
}

namespace RenderPass
{
	bool ApplyConfigurationProperties(const ShaderConfiguration::DocumentDisk&, std::vector<RenderPassDisk>&, std::string&)
	{
		return gConfigurationApplySucceeds;
	}
}

namespace RenderPassRuntime
{
	void PublishRenderPassConfigurations(std::vector<RenderPass::RenderPassDisk>)
	{
		++gPublicationCount;
	}
}

namespace RenderPassTexturePool
{
	void InvalidateHistory() {}
}

namespace ShaderInjectorIO
{
	void WriteToLogFileError(const std::string&) {}
}

int main()
{
	TestInactiveRequestsCoalesceAndHoldTasksUntilFlush();
	TestInactivePublicationWaitsBrieflyWhileWorkContinues();
	TestUrgentPublicationBypassesDelay();
	TestFailedRuntimePublicationFailsHeldTask();

	if (gFailureCount != 0)
		return 1;

	std::cout << "Render pass publication tests passed.\n";
	return 0;
}
