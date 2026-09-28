#include "HookD3D12RuntimeState.h"
#include "../HookD3D12.h"

#include "HookD3D12ExecutionHookHelpers.h"
#include "Globals.h"

namespace HookD3D12
{
	static std::atomic<bool> loggedDispatchMeshHook = false;
	static std::atomic<bool> loggedHiddenMeshDispatch = false;

	void STDMETHODCALLTYPE Hook_DispatchMesh(ID3D12GraphicsCommandList6* commandList, UINT threadGroupCountX, UINT threadGroupCountY, UINT threadGroupCountZ)
	{
		thread_local bool commandHookLogChecked = false;
		if (!commandHookLogChecked)
		{
			LogFirstCommandHookHit(loggedDispatchMeshHook, "Hook_DispatchMesh", static_cast<ID3D12GraphicsCommandList*>(commandList));
			commandHookLogChecked = true;
		}

		ID3D12PipelineState* hiddenPipeline = gHiddenMeshPipelineState.load(std::memory_order_acquire);
		if (hiddenPipeline && Globals::gShaderInjectorEnabled && !IsInsideRenderPassInjection())
		{
			ID3D12GraphicsCommandList* baseCommandList = static_cast<ID3D12GraphicsCommandList*>(commandList);
			ID3D12PipelineState* boundPipeline = GetCommandListPipelineState(baseCommandList).pipelineState.load(std::memory_order_acquire);
			if (boundPipeline == hiddenPipeline)
			{
				LogFirstCommandHookHit(loggedHiddenMeshDispatch, "HiddenMeshDispatch", baseCommandList);
				return;
			}
		}

		Original_DispatchMesh(commandList, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
	}
} //namespace HookD3D12
