#pragma once

#include <string>

#include "RenderPass/DatabaseRenderPasses.h"

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
	bool QueueRenderPassShaderCompile(const std::string& renderPassId, std::string& outError);
	RenderPassShaderBatchCompileResult QueueAllRenderPassShaderCompiles();
	RenderPassCompileProgress GetRenderPassCompileProgress();
	void ProcessCompletedRenderPassCompiles();
	void InvalidateRenderPassCompiles();
} //namespace DatabaseRenderPasses
