#pragma once

#include <d3d12.h>

namespace RenderPassRuntime
{
	//capture the output layout that a replacement pipeline must preserve.
	struct PipelineOutputState
	{
		UINT renderTargetCount = 0;
		DXGI_FORMAT renderTargetFormats[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
		DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_UNKNOWN;
		UINT sampleCount = 1;
		UINT sampleQuality = 0;
	};
} //namespace RenderPassRuntime
