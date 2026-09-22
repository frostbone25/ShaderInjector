// capture pipeline-state streams and preserve the raw stream for later rebuilds.

#include <mutex>
#include <vector>

#include "HookD3D12/HookD3D12.h"
#include "ShaderAutomaticDiscovery.h"

namespace HookD3D12
{
	std::vector<PipelineStateInfo> gPipelineStates;

	void CapturePipelineStateStream(const D3D12_PIPELINE_STATE_STREAM_DESC* pipelineStreamDescription, ID3D12PipelineState* pipelineState)
	{
		if (!pipelineStreamDescription || !pipelineStreamDescription->pPipelineStateSubobjectStream || pipelineStreamDescription->SizeInBytes == 0 || !pipelineState)
			return;

		PipelineStateInfo capturedStreamPipeline{};
		capturedStreamPipeline.pipelineState = pipelineState;

		//preserve the exact byte stream because warm-cache rebuilds need every original subobject.
		const uint8_t* streamDataStart = static_cast<const uint8_t*>(pipelineStreamDescription->pPipelineStateSubobjectStream);
		capturedStreamPipeline.streamBlob.assign(streamDataStart, streamDataStart + pipelineStreamDescription->SizeInBytes);

		ParsePipelineStream(pipelineStreamDescription, capturedStreamPipeline);

		//the stream only borrows this COM pointer, so retain it for future replacement work.
		if (capturedStreamPipeline.rootSignature)
			capturedStreamPipeline.rootSignature->AddRef();

		{
			std::lock_guard<std::mutex> pipelineLock(gPipelineMutex);
			RegisterKnownPipelineStateLocked(pipelineState);
			gPipelineStates.push_back(capturedStreamPipeline);
			RebindPipelineStateInfoPointerFields(gPipelineStates.back());
			QueueShaderTargetApplyWork();
		}

		//analyze after the database entry is stored so discovery can use the captured stream immediately.
		ShaderAutomaticDiscovery::ProcessCapturedStreamPipeline(capturedStreamPipeline);
	}
}
