//DatabaseStreamPSOs.cpp
#include <mutex>
#include <vector>

//custom
#include "DatabaseStreamPSOs.h"
#include "HookD3D12PipelineRegistry.h"
#include "HookD3D12PipelineUtils.h"
#include "ShaderAutomaticDiscovery.h"

namespace HookD3D12
{
	std::vector<PipelineStateInfo> gPipelineStates;

	void CapturePipelineStateStream(const D3D12_PIPELINE_STATE_STREAM_DESC* pipelineStreamDescription, ID3D12PipelineState* pipelineState)
	{
		if (!pipelineStreamDescription || !pipelineStreamDescription->pPipelineStateSubobjectStream || pipelineStreamDescription->SizeInBytes == 0 || !pipelineState)
			return;

		PipelineStateInfo capturedPipeline{};
		capturedPipeline.pipelineState = pipelineState;

		// Keep the raw stream blob; second-run persistence rebuilds need the exact subobject stream captured here.
		const uint8_t* streamStart = (const uint8_t*)pipelineStreamDescription->pPipelineStateSubobjectStream;
		capturedPipeline.streamBlob.assign(streamStart, streamStart + pipelineStreamDescription->SizeInBytes);

		ParsePipelineStream(pipelineStreamDescription, capturedPipeline);

		{
			std::lock_guard<std::mutex> lock(gPipelineMutex);
			RegisterKnownPipelineStateLocked(pipelineState);
			gPipelineStates.push_back(capturedPipeline);
			RebindPipelineStateInfoPointerFields(gPipelineStates.back());
			MarkShaderTargetApplyDirty();
		}

		ShaderAutomaticDiscovery::ProcessCapturedStreamPipeline(capturedPipeline);
	}
}
