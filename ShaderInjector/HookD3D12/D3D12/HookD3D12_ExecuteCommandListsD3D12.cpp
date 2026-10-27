#include "../HookD3D12.h"

#include "HookD3D12RuntimeState.h"
#include "../HookD3D12RetiredPSOTracker.h"
#include "RenderPass/RenderPassRuntime.h"

namespace HookD3D12
{
	void STDMETHODCALLTYPE Hook_ExecuteCommandListsD3D12(ID3D12CommandQueue* commandQueue, UINT commandListCount, ID3D12CommandList* const* commandLists)
	{
		Handle_ExecuteCommandListsD3D12(commandQueue, commandListCount, commandLists);
	}

	void STDMETHODCALLTYPE Handle_ExecuteCommandListsD3D12(ID3D12CommandQueue* commandQueue, UINT commandListCount, ID3D12CommandList* const* commandLists)
	{
		//observed before the submission, so a release proof taken afterwards fences this queue.
		ObserveSubmissionQueue(commandQueue);
		Original_ExecuteCommandListsD3D12(commandQueue, commandListCount, commandLists);

		if (RenderPassRuntime::HasPendingCommandListSubmissionWork())
			RenderPassRuntime::NotifyCommandListsSubmitted(commandQueue, commandListCount, commandLists);

		RememberDirectCommandQueue(commandQueue);
	}
} //namespace HookD3D12
