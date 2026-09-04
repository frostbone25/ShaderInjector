#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "RenderPass/RenderPass.h"

namespace RenderPassTexturePool
{
	struct ReferenceExtent
	{
		ShaderResource::TextureDimension dimension = ShaderResource::TextureDimension::Unknown;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t depth = 1;
		uint32_t arraySize = 1;
		uint32_t mipLevels = 1;
		uint32_t sampleCount = 1;
		DXGI_FORMAT fallbackFormat = DXGI_FORMAT_UNKNOWN;
		DXGI_FORMAT fallbackShaderViewFormat = DXGI_FORMAT_UNKNOWN;
	};

	struct ResolvedTextureDescription
	{
		ShaderResource::TextureDimension dimension = ShaderResource::TextureDimension::Unknown;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		DXGI_FORMAT shaderViewFormat = DXGI_FORMAT_UNKNOWN;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t depth = 1;
		uint32_t arraySize = 1;
		uint32_t mipLevels = 1;
		uint32_t sampleCount = 1;
		ShaderResource::ResourceLifetime lifetime = ShaderResource::ResourceLifetime::Transient;
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
	};

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

	// Overrides are local to one graph execution, never global across command lists
	// or frames. Output writes still resolve the original pooled allocation.
	class ScopedInputTextureOverrides
	{
	public:
		ScopedInputTextureOverrides();
		~ScopedInputTextureOverrides();
		ScopedInputTextureOverrides(const ScopedInputTextureOverrides&) = delete;
		ScopedInputTextureOverrides& operator=(const ScopedInputTextureOverrides&) = delete;
	private:
		size_t previousCount = 0;
	};
	void OverrideInputTexture(
		const std::string& resourceId,
		ShaderResource::TemporalView temporalView,
		const TextureView& texture);
	bool GetInputTexture(
		const std::string& resourceId,
		ShaderResource::TemporalView temporalView,
		TextureView& outTexture);

	void PublishConfigurations(const std::vector<RenderPass::RenderPassDisk>& renderPasses);
	bool EnsurePassResources(
		const RenderPass::RenderPassDisk& renderPass,
		ID3D12GraphicsCommandList* commandList,
		const ReferenceExtent& referenceExtent,
		std::string& outError);
	bool GetTexture(
		const std::string& resourceId,
		ShaderResource::TemporalView temporalView,
		TextureView& outTexture);
	bool EnsureUpsampleChainStages(
		const RenderPass::RenderPassDisk& renderPass,
		ID3D12GraphicsCommandList* commandList,
		const TextureView& sourceTexture,
		const TextureView& destinationTexture,
		bool computePipeline,
		std::vector<TextureView>& outStageTargets,
		std::string& outError);
	void AdvanceFrame();
	void ReleaseResources();
}
