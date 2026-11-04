#pragma once

#include <string>
#include <vector>

#include <d3d12.h>

#include "RenderPass/RenderPass.h"
#include "RenderPass/RenderPassMipChain.h"
#include "ShaderResource/Runtime/ShaderResourcePerformanceStatistics.h"

namespace ShaderResourceRuntime
{
	struct RuntimeTextureBindingOverride
	{
		std::string resourceId;
		D3D12_CPU_DESCRIPTOR_HANDLE shaderResourceView{};
		std::string unorderedAccessResourceId;
		D3D12_CPU_DESCRIPTOR_HANDLE unorderedAccessView{};
	};

	bool BindResources(
		const RenderPass::RenderPassDisk& renderPass,
		ID3D12GraphicsCommandList* commandList,
		const RenderPassMipChain::GraphicsStateSnapshot& gameState,
		const RenderPassMipChain::GraphicsStateSnapshot& oppositePipelineState,
		bool computePipeline,
		std::string& outError,
		const RuntimeTextureBindingOverride* runtimeTextureOverride = nullptr);
	//upload every imported DDS not yet loaded for the device on a private queue and wait for it, leaving each in
	//the COMMON state so any later game command list (direct or compute) can read it.
	void LoadTextures(ID3D12Device* device);
	//rescan the imported DDS folder and replace changed textures, add new ones and drop removed ones.
	void ReloadTextures();
	//the CPU-only SRV of a texture loaded by LoadTextures; false when the texture is not loaded.
	bool FindTextureShaderResourceView(ID3D12Device* device, const std::string& resourceId, D3D12_CPU_DESCRIPTOR_HANDLE& outShaderResourceView);
	void RestoreResources(ID3D12GraphicsCommandList* commandList);
	//flush before another subsystem changes descriptor heaps, even while a graph batch is active.
	void RestoreResourcesImmediately(ID3D12GraphicsCommandList* commandList);
	bool HasRecordedCommandListWork();
	void ResetCommandList(ID3D12GraphicsCommandList* commandList);
	void NotifyCommandListsSubmitted(
		ID3D12CommandQueue* commandQueue,
		UINT commandListCount,
		ID3D12CommandList* const* commandLists);
	void ReleaseResources();
	void LogPerformanceStatistics();
	PerformanceStatistics GetLastPerformanceStatistics();
} //namespace ShaderResourceRuntime
