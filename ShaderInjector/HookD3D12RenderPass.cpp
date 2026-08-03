#include "HookD3D12RenderPass.h"

#include <atomic>

#include "Globals.h"
#include "RenderPassRuntime.h"
#include "ShaderInjectorIO.h"
#include "StringHelper.h"

namespace HookD3D12
{
	namespace
	{
			thread_local unsigned int gRenderPassInjectionDepth = 0;
			std::atomic<bool> gLoggedDrawInstancedHook = false;
			std::atomic<bool> gLoggedDrawIndexedInstancedHook = false;
			std::atomic<bool> gLoggedDispatchHook = false;
			std::atomic<bool> gLoggedExecuteIndirectHook = false;

			void LogFirstCommandHookHit(
				std::atomic<bool>& logged,
				const char* operation,
				ID3D12GraphicsCommandList* commandList)
			{
				if (logged.exchange(true, std::memory_order_relaxed))
					return;

				ShaderInjectorIO::WriteToLogFile(StringHelper::Format(
					"HookD3D12RenderPass->%s: command-list execution hook active commandList=%p type=%u renderPassesEnabled=%u",
					operation,
					commandList,
					commandList ? static_cast<unsigned int>(commandList->GetType()) : UINT_MAX,
					RenderPassRuntime::HasEnabledRenderPasses() ? 1u : 0u));
			}
	}

	ScopedRenderPassInjection::ScopedRenderPassInjection()
	{
		++gRenderPassInjectionDepth;
	}

	ScopedRenderPassInjection::~ScopedRenderPassInjection()
	{
		if (gRenderPassInjectionDepth > 0)
			--gRenderPassInjectionDepth;
	}

	bool IsInsideRenderPassInjection()
	{
		return gRenderPassInjectionDepth != 0;
	}

	void STDMETHODCALLTYPE Hook_DrawInstanced(
		ID3D12GraphicsCommandList* commandList,
		UINT vertexCountPerInstance,
		UINT instanceCount,
		UINT startVertexLocation,
		UINT startInstanceLocation)
	{
		const bool injectedCall = IsInsideRenderPassInjection();
		bool recordedBeforeBoundary = false;
		if (!injectedCall)
		{
			LogFirstCommandHookHit(gLoggedDrawInstancedHook, "Hook_DrawInstanced", commandList);
			recordedBeforeBoundary = Globals::gShaderInjectorEnabled && RenderPassRuntime::ShouldRecordExecutionBoundary(
				commandList,
				false,
				RenderPassRuntime::ExecutionBoundary::Before);
			if (recordedBeforeBoundary)
				RenderPassRuntime::RecordExecutionBoundary(commandList, false, RenderPassRuntime::ExecutionBoundary::Before, "DrawInstanced");
		}

		Original_DrawInstanced(commandList, vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation);
		if (recordedBeforeBoundary)
			RenderPassRuntime::CompleteGraphicsExecutionBoundary(commandList);

		if (Globals::gShaderInjectorEnabled && !injectedCall && RenderPassRuntime::ShouldRecordExecutionBoundary(
			commandList,
			false,
			RenderPassRuntime::ExecutionBoundary::After))
			RenderPassRuntime::RecordExecutionBoundary(commandList, false, RenderPassRuntime::ExecutionBoundary::After, "DrawInstanced");
	}

	void STDMETHODCALLTYPE Hook_DrawIndexedInstanced(
		ID3D12GraphicsCommandList* commandList,
		UINT indexCountPerInstance,
		UINT instanceCount,
		UINT startIndexLocation,
		INT baseVertexLocation,
		UINT startInstanceLocation)
	{
		const bool injectedCall = IsInsideRenderPassInjection();
		bool recordedBeforeBoundary = false;
		if (!injectedCall)
		{
			LogFirstCommandHookHit(gLoggedDrawIndexedInstancedHook, "Hook_DrawIndexedInstanced", commandList);
			recordedBeforeBoundary = Globals::gShaderInjectorEnabled && RenderPassRuntime::ShouldRecordExecutionBoundary(
				commandList,
				false,
				RenderPassRuntime::ExecutionBoundary::Before);
			if (recordedBeforeBoundary)
				RenderPassRuntime::RecordExecutionBoundary(commandList, false, RenderPassRuntime::ExecutionBoundary::Before, "DrawIndexedInstanced");
		}

		Original_DrawIndexedInstanced(commandList, indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);
		if (recordedBeforeBoundary)
			RenderPassRuntime::CompleteGraphicsExecutionBoundary(commandList);

		if (Globals::gShaderInjectorEnabled && !injectedCall && RenderPassRuntime::ShouldRecordExecutionBoundary(
			commandList,
			false,
			RenderPassRuntime::ExecutionBoundary::After))
			RenderPassRuntime::RecordExecutionBoundary(commandList, false, RenderPassRuntime::ExecutionBoundary::After, "DrawIndexedInstanced");
	}

	void STDMETHODCALLTYPE Hook_Dispatch(
		ID3D12GraphicsCommandList* commandList,
		UINT threadGroupCountX,
		UINT threadGroupCountY,
		UINT threadGroupCountZ)
	{
		const bool injectedCall = IsInsideRenderPassInjection();
		if (!injectedCall)
		{
			LogFirstCommandHookHit(gLoggedDispatchHook, "Hook_Dispatch", commandList);
			if (Globals::gShaderInjectorEnabled && RenderPassRuntime::ShouldRecordExecutionBoundary(
				commandList,
				true,
				RenderPassRuntime::ExecutionBoundary::Before))
				RenderPassRuntime::RecordExecutionBoundary(commandList, true, RenderPassRuntime::ExecutionBoundary::Before, "Dispatch");
		}

		Original_Dispatch(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);

		if (Globals::gShaderInjectorEnabled && !injectedCall && RenderPassRuntime::ShouldRecordExecutionBoundary(
			commandList,
			true,
			RenderPassRuntime::ExecutionBoundary::After))
			RenderPassRuntime::RecordExecutionBoundary(commandList, true, RenderPassRuntime::ExecutionBoundary::After, "Dispatch");
	}

	void STDMETHODCALLTYPE Hook_IASetPrimitiveTopology(
		ID3D12GraphicsCommandList* commandList,
		D3D12_PRIMITIVE_TOPOLOGY primitiveTopology)
	{
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection())
			RenderPassRuntime::TrackPrimitiveTopology(commandList, primitiveTopology);
		Original_IASetPrimitiveTopology(commandList, primitiveTopology);
	}

	void STDMETHODCALLTYPE Hook_RSSetViewports(
		ID3D12GraphicsCommandList* commandList,
		UINT viewportCount,
		const D3D12_VIEWPORT* viewports)
	{
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() &&
			RenderPassRuntime::IsGraphicsStateTrackingRequired())
		{
			RenderPassRuntime::TrackViewports(commandList, viewportCount, viewports);
		}
		Original_RSSetViewports(commandList, viewportCount, viewports);
	}

	void STDMETHODCALLTYPE Hook_RSSetScissorRects(
		ID3D12GraphicsCommandList* commandList,
		UINT rectangleCount,
		const D3D12_RECT* rectangles)
	{
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() &&
			RenderPassRuntime::IsGraphicsStateTrackingRequired())
		{
			RenderPassRuntime::TrackScissorRectangles(commandList, rectangleCount, rectangles);
		}
		Original_RSSetScissorRects(commandList, rectangleCount, rectangles);
	}

	void STDMETHODCALLTYPE Hook_SetDescriptorHeaps(
		ID3D12GraphicsCommandList* commandList,
		UINT descriptorHeapCount,
		ID3D12DescriptorHeap* const* descriptorHeaps)
	{
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() &&
			RenderPassRuntime::IsGraphicsStateTrackingRequired())
			RenderPassRuntime::TrackDescriptorHeaps(commandList, descriptorHeapCount, descriptorHeaps);
		Original_SetDescriptorHeaps(commandList, descriptorHeapCount, descriptorHeaps);
	}

	void STDMETHODCALLTYPE Hook_SetComputeRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle)
	{
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() &&
			RenderPassRuntime::IsResourceTrackingRequired())
			RenderPassRuntime::TrackRootDescriptorTable(commandList, true, rootParameterIndex, descriptorHandle);
		Original_SetComputeRootDescriptorTable(commandList, rootParameterIndex, descriptorHandle);
	}

	void STDMETHODCALLTYPE Hook_SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle)
	{
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() &&
			RenderPassRuntime::IsGraphicsStateTrackingRequired())
			RenderPassRuntime::TrackRootDescriptorTable(commandList, false, rootParameterIndex, descriptorHandle);
		Original_SetGraphicsRootDescriptorTable(commandList, rootParameterIndex, descriptorHandle);
	}

	void STDMETHODCALLTYPE Hook_SetComputeRoot32BitConstant(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, UINT value, UINT destinationOffset)
	{
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() && RenderPassRuntime::IsResourceTrackingRequired())
			RenderPassRuntime::TrackRootConstants(commandList, true, rootParameterIndex, 1, &value, destinationOffset);
		Original_SetComputeRoot32BitConstant(commandList, rootParameterIndex, value, destinationOffset);
	}

	void STDMETHODCALLTYPE Hook_SetGraphicsRoot32BitConstant(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, UINT value, UINT destinationOffset)
	{
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() && RenderPassRuntime::IsGraphicsStateTrackingRequired())
			RenderPassRuntime::TrackRootConstants(commandList, false, rootParameterIndex, 1, &value, destinationOffset);
		Original_SetGraphicsRoot32BitConstant(commandList, rootParameterIndex, value, destinationOffset);
	}

	void STDMETHODCALLTYPE Hook_SetComputeRoot32BitConstants(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, UINT valueCount, const void* values, UINT destinationOffset)
	{
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() && RenderPassRuntime::IsResourceTrackingRequired())
			RenderPassRuntime::TrackRootConstants(commandList, true, rootParameterIndex, valueCount, values, destinationOffset);
		Original_SetComputeRoot32BitConstants(commandList, rootParameterIndex, valueCount, values, destinationOffset);
	}

	void STDMETHODCALLTYPE Hook_SetGraphicsRoot32BitConstants(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, UINT valueCount, const void* values, UINT destinationOffset)
	{
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() && RenderPassRuntime::IsGraphicsStateTrackingRequired())
			RenderPassRuntime::TrackRootConstants(commandList, false, rootParameterIndex, valueCount, values, destinationOffset);
		Original_SetGraphicsRoot32BitConstants(commandList, rootParameterIndex, valueCount, values, destinationOffset);
	}

#define DEFINE_ROOT_DESCRIPTOR_HOOK(HookName, OriginalName, IsCompute, BindingName) \
	void STDMETHODCALLTYPE HookName(ID3D12GraphicsCommandList* commandList, UINT rootParameterIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress) \
	{ \
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() && \
			(IsCompute ? RenderPassRuntime::IsResourceTrackingRequired() : RenderPassRuntime::IsGraphicsStateTrackingRequired())) \
			RenderPassRuntime::TrackRootDescriptor(commandList, IsCompute, BindingName, rootParameterIndex, gpuAddress); \
		OriginalName(commandList, rootParameterIndex, gpuAddress); \
	}

	DEFINE_ROOT_DESCRIPTOR_HOOK(Hook_SetComputeRootConstantBufferView, Original_SetComputeRootConstantBufferView, true, "CBV")
	DEFINE_ROOT_DESCRIPTOR_HOOK(Hook_SetGraphicsRootConstantBufferView, Original_SetGraphicsRootConstantBufferView, false, "CBV")
	DEFINE_ROOT_DESCRIPTOR_HOOK(Hook_SetComputeRootShaderResourceView, Original_SetComputeRootShaderResourceView, true, "SRV")
	DEFINE_ROOT_DESCRIPTOR_HOOK(Hook_SetGraphicsRootShaderResourceView, Original_SetGraphicsRootShaderResourceView, false, "SRV")
	DEFINE_ROOT_DESCRIPTOR_HOOK(Hook_SetComputeRootUnorderedAccessView, Original_SetComputeRootUnorderedAccessView, true, "UAV")
	DEFINE_ROOT_DESCRIPTOR_HOOK(Hook_SetGraphicsRootUnorderedAccessView, Original_SetGraphicsRootUnorderedAccessView, false, "UAV")

#undef DEFINE_ROOT_DESCRIPTOR_HOOK

	void STDMETHODCALLTYPE Hook_IASetIndexBuffer(ID3D12GraphicsCommandList* commandList, const D3D12_INDEX_BUFFER_VIEW* view)
	{
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() && RenderPassRuntime::IsResourceTrackingRequired())
			RenderPassRuntime::TrackIndexBuffer(commandList, view);
		Original_IASetIndexBuffer(commandList, view);
	}

	void STDMETHODCALLTYPE Hook_IASetVertexBuffers(ID3D12GraphicsCommandList* commandList, UINT startSlot, UINT viewCount, const D3D12_VERTEX_BUFFER_VIEW* views)
	{
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() && RenderPassRuntime::IsResourceTrackingRequired())
			RenderPassRuntime::TrackVertexBuffers(commandList, startSlot, viewCount, views);
		Original_IASetVertexBuffers(commandList, startSlot, viewCount, views);
	}

	void STDMETHODCALLTYPE Hook_OMSetRenderTargets(ID3D12GraphicsCommandList* commandList, UINT renderTargetCount, const D3D12_CPU_DESCRIPTOR_HANDLE* renderTargetDescriptors, BOOL descriptorsAreContiguous, const D3D12_CPU_DESCRIPTOR_HANDLE* depthStencilDescriptor)
	{
		if (Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection() &&
			RenderPassRuntime::IsGraphicsStateTrackingRequired())
		{
			RenderPassRuntime::TrackRenderTargets(
				commandList,
				renderTargetCount,
				renderTargetDescriptors,
				descriptorsAreContiguous,
				depthStencilDescriptor);
		}
		Original_OMSetRenderTargets(commandList, renderTargetCount, renderTargetDescriptors, descriptorsAreContiguous, depthStencilDescriptor);
	}

	void STDMETHODCALLTYPE Hook_ExecuteIndirect(
		ID3D12GraphicsCommandList* commandList,
		ID3D12CommandSignature* commandSignature,
		UINT maximumCommandCount,
		ID3D12Resource* argumentBuffer,
		UINT64 argumentBufferOffset,
		ID3D12Resource* countBuffer,
		UINT64 countBufferOffset)
	{
		const bool injectedCall = IsInsideRenderPassInjection();
		bool recordedBeforeBoundary = false;
		if (!injectedCall)
		{
			LogFirstCommandHookHit(gLoggedExecuteIndirectHook, "Hook_ExecuteIndirect", commandList);
			recordedBeforeBoundary = Globals::gShaderInjectorEnabled && RenderPassRuntime::ShouldRecordExecutionBoundary(
				commandList,
				false,
				RenderPassRuntime::ExecutionBoundary::Before);
			if (recordedBeforeBoundary)
				RenderPassRuntime::RecordExecutionBoundary(commandList, false, RenderPassRuntime::ExecutionBoundary::Before, "ExecuteIndirect");
		}

		Original_ExecuteIndirect(
			commandList,
			commandSignature,
			maximumCommandCount,
			argumentBuffer,
			argumentBufferOffset,
			countBuffer,
			countBufferOffset);
		if (recordedBeforeBoundary)
			RenderPassRuntime::CompleteGraphicsExecutionBoundary(commandList);

		if (Globals::gShaderInjectorEnabled && !injectedCall && RenderPassRuntime::ShouldRecordExecutionBoundary(
			commandList,
			false,
			RenderPassRuntime::ExecutionBoundary::After))
			RenderPassRuntime::RecordExecutionBoundary(commandList, false, RenderPassRuntime::ExecutionBoundary::After, "ExecuteIndirect");
	}
}
