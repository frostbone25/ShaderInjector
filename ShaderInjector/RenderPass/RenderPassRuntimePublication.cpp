#include "RenderPass/DatabaseRenderPasses.h"

#include <chrono>
#include <utility>
#include <vector>

#include "ModifiedShader/DatabaseModifiedShaders.h"
#include "RenderPass/RenderPassConfigurationProperties.h"
#include "RenderPass/RenderPassRuntime.h"
#include "RenderPass/RenderPassShaderCompileQueue.h"
#include "RenderPass/RenderPassTexturePool.h"
#include "IO/ShaderInjectorIO.h"
#include "ShaderConfiguration/ConfigurationProgressTask.h"
#include "ShaderConfiguration/DatabaseShaderConfigurations.h"

namespace DatabaseRenderPasses
{
	namespace
	{
		bool gRuntimeShaderPublicationPending = false;
		bool gRuntimeShaderPublicationUrgent = false;
		std::chrono::steady_clock::time_point gRuntimeShaderPublicationRequestedAt;
		std::vector<std::shared_ptr<ShaderConfiguration::ConfigurationProgressTask>> gRuntimeShaderPublicationTasks;

		bool TryPublishRuntimeConfiguration(bool invalidateHistory)
		{
			std::vector<RenderPass::RenderPassDisk> effectivePasses = gRenderPasses;
			std::string error;
			const ShaderConfiguration::DocumentDisk appliedDocument = DatabaseShaderConfigurations::GetAppliedDocument();

			if (!RenderPass::ApplyConfigurationProperties(appliedDocument, effectivePasses, error))
			{
				ShaderInjectorIO::WriteToLogFileError("DatabaseRenderPasses->PublishRuntimeConfiguration: " + error);
				return false;
			}

			RenderPassRuntime::PublishRenderPassConfigurations(std::move(effectivePasses));

			if (invalidateHistory)
				RenderPassTexturePool::InvalidateHistory();

			return true;
		}
	}

	void PublishRuntimeConfiguration(bool invalidateHistory)
	{
		TryPublishRuntimeConfiguration(invalidateHistory);
	}

	void RequestRuntimeShaderPublication(bool urgent, std::shared_ptr<ShaderConfiguration::ConfigurationProgressTask> progressTask)
	{
		if (!gRuntimeShaderPublicationPending)
		{
			//start the cap at the first inactive change so a steady stream cannot keep moving it.
			gRuntimeShaderPublicationRequestedAt = std::chrono::steady_clock::now();
		}

		gRuntimeShaderPublicationPending = true;
		gRuntimeShaderPublicationUrgent = gRuntimeShaderPublicationUrgent || urgent;

		if (progressTask)
		{
			//the active shader is not ready until its runtime snapshot can see the committed blobs.
			gRuntimeShaderPublicationTasks.push_back(std::move(progressTask));
		}
	}

	void FlushPendingRuntimeShaderPublication()
	{
		if (!gRuntimeShaderPublicationPending)
			return;

		const DatabaseModifiedShaders::CompileProgress modifiedShaderProgress = DatabaseModifiedShaders::GetCompileProgress();
		const RenderPassCompileProgress renderPassProgress = GetRenderPassCompileProgress();
		const bool modifiedShaderWorkPending = modifiedShaderProgress.queued > 0 || modifiedShaderProgress.running > 0 ||
			modifiedShaderProgress.completed < modifiedShaderProgress.total;
		const bool renderPassWorkPending = renderPassProgress.queued > 0 || renderPassProgress.running > 0 ||
			renderPassProgress.completed < renderPassProgress.total;
		const bool workDrained = !modifiedShaderWorkPending && !renderPassWorkPending;
		//coalesce inactive snapshots briefly, but let a busy background batch publish at least every 200ms.
		const bool publicationDelayExpired = std::chrono::steady_clock::now() - gRuntimeShaderPublicationRequestedAt >= std::chrono::milliseconds(200);

		if (!gRuntimeShaderPublicationUrgent && !workDrained && !publicationDelayExpired)
			return;

		gRuntimeShaderPublicationPending = false;
		gRuntimeShaderPublicationUrgent = false;
		const bool published = TryPublishRuntimeConfiguration(false);

		//release every held compile token only after this snapshot build has either succeeded or failed.
		for (const std::shared_ptr<ShaderConfiguration::ConfigurationProgressTask>& progressTask : gRuntimeShaderPublicationTasks)
		{
			if (!progressTask)
				continue;

			if (published)
				progressTask->MarkSucceeded();
			else
				progressTask->MarkFailed();
		}

		gRuntimeShaderPublicationTasks.clear();
	}
} //namespace DatabaseRenderPasses
