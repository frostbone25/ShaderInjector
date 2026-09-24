#pragma once

#include <d3d12.h>

namespace RenderPassExecutor
{
	//record the color formats and sample layout required by a replacement pipeline.
	struct RenderTargetState
	{
		UINT count = 0;
		DXGI_FORMAT formats[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
		UINT sampleCount = 1;
		UINT sampleQuality = 0;
	};
} //namespace RenderPassExecutor
