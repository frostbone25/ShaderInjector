#pragma once

#include <d3d12.h>

#include "Enum/PipelineSourceList.h"

namespace HookD3D12
{
	struct PSOPendingRebuild
	{
		PipelineSourceList source;
		int index;
		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE targetType;
	};
}
