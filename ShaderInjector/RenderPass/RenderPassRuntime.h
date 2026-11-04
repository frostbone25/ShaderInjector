#pragma once
#include "Enum/RenderPassRuntimeExecutionBoundary.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <d3d12.h>

#include "RenderPass/RenderPass.h"
#include "RenderPass/PipelineOutputState.h"
#include "RenderPass/Runtime/ShaderPatchBinding.h"
#include "RenderPass/Runtime/PerformanceSnapshot.h"
#include "ShaderTarget/ShaderTarget.h"

namespace RenderPassRuntime
{
	void PublishRenderPassConfigurations(std::vector<RenderPass::RenderPassDisk> renderPassConfigurations);
	bool HasEnabledRenderPasses();
	bool HasEnabledMipChainPasses();
	bool IsTrackingRequired();
	bool IsResourceTrackingRequired();
	bool IsRootBindingTrackingRequired();
	bool IsGameTextureDescriptorTrackingRequired();
	bool IsDescriptorTableTrackingRequired();
	bool IsDescriptorRegistryTrackingRequired();
	bool IsGraphicsStateTrackingRequired();
	bool IsPipelineExecutionTrackingRequired(bool computePipeline);
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
	//shader patches that add textures are published with the shader-target bindings and
	//execute at the same boundaries; a pipeline with a shader-target binding never uses one.
	void AddShaderPatchBinding(
		ID3D12PipelineState* pipelineState,
		std::shared_ptr<const ShaderPatchBinding> binding);
	void CommitShaderTargetBindingUpdate();
	//texture recipes need the root-binding hooks even when no render pass is enabled.
	void SetShaderPatchTrackingRequired(bool required);
	bool IsShaderPatchTrackingRequired();
	//classify ExecuteIndirect signatures; unknown signatures are treated as graphics draws that write root arguments.
	void TrackCommandSignature(ID3D12CommandSignature* commandSignature, const D3D12_COMMAND_SIGNATURE_DESC& description);
	void FindCommandSignature(ID3D12CommandSignature* commandSignature, bool& outComputeDispatch, bool& outWritesRootArguments);
	//record the root arguments an ExecuteIndirect reset, so later replays match the game's state.
	void TrackExecuteIndirect(ID3D12GraphicsCommandList* commandList, ID3D12CommandSignature* commandSignature, bool computePipeline);

	//the root-argument and heap hooks install after the overlay is ready; lists recorded before that are not fully tracked.
	void NotifyCommandListTrackingHooksInstalled();
	void ResetCommandList(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* initialPipelineState);
	void CompleteCommandListReset(ID3D12GraphicsCommandList* commandList, bool resetSucceeded);
	void TrackPipelineState(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* pipelineState);
	void RecordShaderTargetUse(ID3D12PipelineState* pipelineState);
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
		const char* operationName,
		UINT originalThreadGroupCountX = 0,
		UINT originalThreadGroupCountY = 0,
		UINT originalThreadGroupCountZ = 0,
		bool indirectExecution = false,
		bool indirectWritesRootArguments = false);
	void CompleteGraphicsExecutionBoundary(ID3D12GraphicsCommandList* commandList);
	void CompleteComputeExecutionBoundary(ID3D12GraphicsCommandList* commandList);
	void NotifyCommandListsSubmitted(
		ID3D12CommandQueue* commandQueue,
		UINT commandListCount,
		ID3D12CommandList* const* commandLists);
	void AdvanceFrame();
	void LogPerformanceSnapshot();
	PerformanceSnapshot GetPerformanceSnapshot();

	RenderPass::RuntimeDiagnostics GetDiagnostics(const std::string& renderPassId);
	void ClearDiagnostics(const std::string& renderPassId);
} //namespace RenderPassRuntime
