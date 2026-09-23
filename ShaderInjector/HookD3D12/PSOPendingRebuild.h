#pragma once

#include <d3d12.h>

#include "Enum/PipelineSourceList.h"

namespace HookD3D12
{
	struct PSOPendingRebuild
	{
		PipelineSourceList pipelineSource;
		int pipelineIndex;
		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE targetSubobjectType;
	};
}
