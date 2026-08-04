#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>

#include "RenderPass.h"
#include "ShaderTarget.h"

namespace RenderPassRuntime
{
	struct PipelineOutputState
	{
		UINT renderTargetCount = 0;
		DXGI_FORMAT renderTargetFormats[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
		DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_UNKNOWN;
		UINT sampleCount = 1;
		UINT sampleQuality = 0;
	};

	enum class ExecutionBoundary
	{
		Before,
		After,
	};

	void PublishRenderPassConfigurations(const std::vector<RenderPass::RenderPassDisk>& renderPasses);
	bool HasEnabledRenderPasses();
	bool HasEnabledMipChainPasses();
	bool IsTrackingRequired();
	bool IsResourceTrackingRequired();
	bool IsDescriptorRegistryTrackingRequired();
	bool IsGraphicsStateTrackingRequired();
	bool HasPendingCommandListSubmissionWork();
	bool IsExecutionTrackingRequired(bool computePipeline, ExecutionBoundary boundary);
	uint32_t GetExecutionBoundaryMask(
		ID3D12GraphicsCommandList* commandList,
		bool computePipeline);
	bool ShouldRecordExecutionBoundary(
		ID3D12GraphicsCommandList* commandList,
		bool computePipeline,
		ExecutionBoundary boundary);

	void BeginShaderTargetBindingUpdate();
	void AddShaderTargetBinding(
		ID3D12PipelineState* pipelineState,
		const std::string& modifiedShaderId,
		const std::string& shaderTargetName,
		uint64_t shaderTargetHash,
		ShaderTarget::ShaderType shaderTargetType,
		const PipelineOutputState& outputState = {});
	void CommitShaderTargetBindingUpdate();

	void ResetCommandList(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* initialPipelineState);
	void CompleteCommandListReset(ID3D12GraphicsCommandList* commandList, bool resetSucceeded);
	void TrackPipelineState(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* pipelineState);
	void TrackBoundPipelineState(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* pipelineState);
	void TrackPrimitiveTopology(ID3D12GraphicsCommandList* commandList, D3D12_PRIMITIVE_TOPOLOGY primitiveTopology);
	void TrackRootSignature(
		ID3D12GraphicsCommandList* commandList,
		bool computePipeline,
		ID3D12RootSignature* rootSignature);
	void TrackDescriptorHeaps(
		ID3D12GraphicsCommandList* commandList,
		UINT descriptorHeapCount,
		ID3D12DescriptorHeap* const* descriptorHeaps);
	void TrackRootDescriptorTable(
		ID3D12GraphicsCommandList* commandList,
		bool computePipeline,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle);
	void TrackRootDescriptor(
		ID3D12GraphicsCommandList* commandList,
		bool computePipeline,
		const char* bindingType,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS gpuAddress);
	void TrackRootConstants(
		ID3D12GraphicsCommandList* commandList,
		bool computePipeline,
		UINT rootParameterIndex,
		UINT valueCount,
		const void* values,
		UINT destinationOffset);
	void TrackIndexBuffer(
		ID3D12GraphicsCommandList* commandList,
		const D3D12_INDEX_BUFFER_VIEW* view);
	void TrackVertexBuffers(
		ID3D12GraphicsCommandList* commandList,
		UINT startSlot,
		UINT viewCount,
		const D3D12_VERTEX_BUFFER_VIEW* views);
	void TrackRenderTargets(
		ID3D12GraphicsCommandList* commandList,
		UINT renderTargetCount,
		const D3D12_CPU_DESCRIPTOR_HANDLE* renderTargetDescriptors,
		BOOL descriptorsAreContiguous,
		const D3D12_CPU_DESCRIPTOR_HANDLE* depthStencilDescriptor);
	void TrackViewports(
		ID3D12GraphicsCommandList* commandList,
		UINT viewportCount,
		const D3D12_VIEWPORT* viewports);
	void TrackScissorRectangles(
		ID3D12GraphicsCommandList* commandList,
		UINT rectangleCount,
		const D3D12_RECT* rectangles);

	void RecordExecutionBoundary(
		ID3D12GraphicsCommandList* commandList,
		bool computePipeline,
		ExecutionBoundary boundary,
		const char* operationName);
	void CompleteGraphicsExecutionBoundary(ID3D12GraphicsCommandList* commandList);
	void NotifyCommandListsSubmitted(
		ID3D12CommandQueue* commandQueue,
		UINT commandListCount,
		ID3D12CommandList* const* commandLists);

	RenderPass::RuntimeDiagnostics GetDiagnostics(const std::string& renderPassId);
	void ClearDiagnostics(const std::string& renderPassId);
}
