#include "HookD3D12HookHandlers.h"

#include "HookD3D12RuntimeState.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_ExecuteCommandListsD3D12(ID3D12CommandQueue* commandQueue, UINT commandListCount, ID3D12CommandList* const* commandLists)
	{
		Handle_ExecuteCommandListsD3D12(commandQueue, commandListCount, commandLists);
	}

	void STDMETHODCALLTYPE Handle_ExecuteCommandListsD3D12(ID3D12CommandQueue* commandQueue, UINT commandListCount, ID3D12CommandList* const* commandLists)
	{
		Original_ExecuteCommandListsD3D12(commandQueue, commandListCount, commandLists);

		if (RenderPassRuntime::HasPendingCommandListSubmissionWork())
			RenderPassRuntime::NotifyCommandListsSubmitted(commandQueue, commandListCount, commandLists);

		RememberDirectCommandQueue(commandQueue);
	}
}
