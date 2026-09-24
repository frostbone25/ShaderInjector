#pragma once

#include <d3d12.h>

namespace ShaderResourceRuntime
{
	//return the CPU and GPU handles for one newly reserved descriptor range.
	struct DescriptorAllocation
	{
		ID3D12DescriptorHeap* heap = nullptr;
		D3D12_CPU_DESCRIPTOR_HANDLE cpuStart{};
		D3D12_GPU_DESCRIPTOR_HANDLE gpuStart{};
	};
} //namespace ShaderResourceRuntime
