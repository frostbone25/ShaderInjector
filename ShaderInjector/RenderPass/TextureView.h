#pragma once

#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

#include "RenderPass/ResolvedTextureDescription.h"

namespace RenderPassTexturePool
{
	//carry a runtime texture and the views needed to bind it in a pass.
	struct TextureView
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> shaderViewHeap;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> renderTargetViewHeap;
		D3D12_CPU_DESCRIPTOR_HANDLE shaderResourceView{};
		D3D12_CPU_DESCRIPTOR_HANDLE unorderedAccessView{};
		D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView{};
		ResolvedTextureDescription description;
		D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
		uint64_t generation = 0;
	};
} //namespace RenderPassTexturePool
