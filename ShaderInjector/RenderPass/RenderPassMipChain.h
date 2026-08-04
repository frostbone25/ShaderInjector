#pragma once

#include <string>
#include <vector>

#include <d3d12.h>

#include "RenderPass.h"

namespace RenderPassMipChain
{
	struct DescriptorHeapBinding
	{
		ID3D12DescriptorHeap* heap = nullptr;
		D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
		UINT descriptorCount = 0;
		UINT descriptorIncrementSize = 0;
		D3D12_CPU_DESCRIPTOR_HANDLE cpuStart{};
		D3D12_GPU_DESCRIPTOR_HANDLE gpuStart{};
	};

	struct GraphicsStateSnapshot
	{
		ID3D12RootSignature* rootSignature = nullptr;
		ID3D12PipelineState* pipelineState = nullptr;
		D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
		std::vector<DescriptorHeapBinding> descriptorHeaps;
		std::vector<RenderPass::ResourceBindingDiagnostic> rootBindings;
		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> renderTargets;
		D3D12_CPU_DESCRIPTOR_HANDLE depthStencil{};
		std::vector<D3D12_VIEWPORT> viewports;
		std::vector<D3D12_RECT> scissorRectangles;
	};

	struct ExecutionResult
	{
		std::string renderPassId;
		bool attempted = false;
		bool succeeded = false;
		std::string error;
	};

	std::vector<ExecutionResult> PrepareForTargetDraw(
		const std::vector<const RenderPass::RenderPassDisk*>& renderPasses,
		ID3D12GraphicsCommandList* commandList,
		const GraphicsStateSnapshot& gameState);
	void RestoreAfterTargetDraw(ID3D12GraphicsCommandList* commandList);
	bool HasRecordedCommandListWork();
	void ResetCommandListRecording(ID3D12GraphicsCommandList* commandList);
	void NotifyCommandListsSubmitted(
		ID3D12CommandQueue* commandQueue,
		UINT commandListCount,
		ID3D12CommandList* const* commandLists);
	void ReleaseResources();
}
