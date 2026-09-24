#pragma once

#include <string>

#include <d3d12.h>
#include <wrl/client.h>

namespace RenderPassMipChain
{
	//own the generated mip texture and all views used to read or write its levels.
	struct MipTextureResources
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> texture;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> sourceHeap;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> renderTargetHeap;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> fullMipChainHeap;
		UINT width = 0;
		UINT height = 0;
		UINT mipLevelCount = 0;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		UINT shader4ComponentMapping = 0;
		D3D12_CPU_DESCRIPTOR_HANDLE fullMipChainDescriptor{};
		bool fullMipChainDescriptorInitialized = false;
		bool allSubresourcesShaderReadable = false;
		bool computePipeline = false;
		std::wstring eventName;
	};
} //namespace RenderPassMipChain
