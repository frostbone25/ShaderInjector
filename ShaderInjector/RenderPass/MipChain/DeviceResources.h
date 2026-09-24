#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>

#include <d3d12.h>
#include <wrl/client.h>

namespace RenderPassMipChain
{
	//reuse mip generator pipelines and format support results for one D3D12 device.
	struct DeviceResources
	{
		Microsoft::WRL::ComPtr<ID3D12Device> device;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
		std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> pipelines;
		std::unordered_map<std::string, std::string> pipelineErrors;
		std::array<std::atomic<int8_t>, 256> graphicsMipGeneratorFormatSupport;
		std::array<std::atomic<int8_t>, 256> computeMipGeneratorFormatSupport;
		UINT shaderResourceDescriptorIncrement = 0;
		UINT renderTargetDescriptorIncrement = 0;

		DeviceResources()
		{
			//minus one means this format has not been checked against the device yet.
			for (std::atomic<int8_t>& formatSupport : graphicsMipGeneratorFormatSupport)
				formatSupport.store(-1, std::memory_order_relaxed);
			for (std::atomic<int8_t>& formatSupport : computeMipGeneratorFormatSupport)
				formatSupport.store(-1, std::memory_order_relaxed);
		}
	};
} //namespace RenderPassMipChain
