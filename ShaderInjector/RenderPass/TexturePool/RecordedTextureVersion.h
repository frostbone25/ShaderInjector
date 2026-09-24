#pragma once

#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

namespace RenderPassTexturePool
{
	//keep resources alive after recording until their submitted GPU work retires.
	struct RecordedTextureVersion
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> shaderViewHeap;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> renderTargetViewHeap;
		uint64_t allocationBytes = 0;
	};
} //namespace RenderPassTexturePool
