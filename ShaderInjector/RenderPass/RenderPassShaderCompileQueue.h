#pragma once

#include <string>
#include <memory>

#include "RenderPass/DatabaseRenderPasses.h"

namespace ShaderConfiguration
{
	class CompileConfiguration;
	class ConfigurationProgress;
}

namespace DatabaseRenderPasses
{
	struct RenderPassCompileProgress
	{
		int total = 0;
		//completed work includes failed and discarded jobs so the bar always reaches the end.
		int completed = 0;
		int failed = 0;
		//queued also includes target variants waiting for the shared compiler to publish their results.
		int queued = 0;
		int running = 0;
		int discarded = 0;
	};

	//queue, drain, and invalidate on the frame thread; workers only touch owned shader snapshots.
	bool QueueRenderPassShaderCompile(const std::string& renderPassId, std::string& outError, std::shared_ptr<const ShaderConfiguration::CompileConfiguration> previousConfiguration = {});
	//passing the previous snapshot limits property applies to passes whose source dependencies changed.
	RenderPassShaderBatchCompileResult QueueAllRenderPassShaderCompiles(std::shared_ptr<const ShaderConfiguration::CompileConfiguration> previousConfiguration = {});
	RenderPassCompileProgress GetRenderPassCompileProgress();
	void ProcessCompletedRenderPassCompiles();
	void CancelConfigurationCompiles(std::shared_ptr<ShaderConfiguration::ConfigurationProgress> progress);
	void InvalidateRenderPassCompiles();
} //namespace DatabaseRenderPasses
