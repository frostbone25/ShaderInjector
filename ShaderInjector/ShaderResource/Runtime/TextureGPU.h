#pragma once

#include <d3d12.h>
#include <wrl/client.h>

namespace ShaderResourceRuntime
{
	//keep the uploaded texture and its shader view alive while passes reference it.
	struct TextureGPU
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> texture;
		Microsoft::WRL::ComPtr<ID3D12Resource> upload;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> shaderResourceViewHeap;
		D3D12_SHADER_RESOURCE_VIEW_DESC view{};
		bool uploadRecorded = false;
	};
} //namespace ShaderResourceRuntime
