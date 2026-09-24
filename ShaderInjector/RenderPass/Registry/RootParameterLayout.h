#pragma once

#include <vector>

#include <d3d12.h>

#include "RenderPass/Registry/DescriptorRangeLayout.h"

namespace RenderPassResourceRegistry
{
	//keep one root parameter's descriptor ranges and shader visibility together.
	struct RootParameterLayout
	{
		D3D12_ROOT_PARAMETER_TYPE type = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		D3D12_SHADER_VISIBILITY shaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		UINT shaderRegister = UINT32_MAX;
		UINT registerSpace = UINT32_MAX;
		std::vector<DescriptorRangeLayout> ranges;
	};
} //namespace RenderPassResourceRegistry
