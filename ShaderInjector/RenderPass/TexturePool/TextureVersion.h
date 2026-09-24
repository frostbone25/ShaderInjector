#pragma once

#include <d3d12.h>
#include <wrl/client.h>

namespace RenderPassTexturePool
{
	//hold one temporal version and the descriptor views used to read or write it.
	struct TextureVersion
	{
		bool written = false;
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> shaderViewHeap;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> renderTargetViewHeap;
		D3D12_CPU_DESCRIPTOR_HANDLE shaderResourceView{};
		D3D12_CPU_DESCRIPTOR_HANDLE unorderedAccessView{};
		D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView{};
	};
} //namespace RenderPassTexturePool
