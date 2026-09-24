#pragma once

#include <d3d12.h>

namespace ShaderResourceRuntime
{
	//restore one root descriptor table after a resource-binding pass finishes.
	struct RootTableRestore
	{
		UINT rootParameterIndex = UINT32_MAX;
		D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle{};
		bool computePipeline = false;
	};
} //namespace ShaderResourceRuntime
