#include "HookD3D12HookHandlers.h"

#include <atomic>

#include "HookD3D12RuntimeState.h"
#include "GUI/ShaderInjectorGUI.h"
#include "IO/ShaderInjectorIO.h"
#include "StringHelper.h"

namespace HookD3D12
{
	std::atomic<uint64_t> gCreatePipelineStateFailureCount = 0;

	HRESULT STDMETHODCALLTYPE Hook_CreatePipelineState(ID3D12Device2* device, const D3D12_PIPELINE_STATE_STREAM_DESC* description, REFIID interfaceId, void** pipelineState)
	{
		return Handle_CreatePipelineState(device, description, interfaceId, pipelineState);
	}

	HRESULT STDMETHODCALLTYPE Handle_CreatePipelineState(ID3D12Device2* device, const D3D12_PIPELINE_STATE_STREAM_DESC* description, REFIID interfaceId, void** pipelineState)
	{
		ScopedPipelineActivity pipelineActivity;
		HRESULT result = Original_CreatePipelineState(device, description, interfaceId, pipelineState);

		if (FAILED(result))
		{
			const uint64_t failureCount = gCreatePipelineStateFailureCount.fetch_add(1, std::memory_order_relaxed) + 1;
			const bool shouldLog = failureCount <= 4 || (failureCount & (failureCount - 1)) == 0;

			if (shouldLog)
			{
				const HRESULT removedReason = device ? device->GetDeviceRemovedReason() : E_POINTER;

				ShaderInjectorIO::WriteToLogFileError(StringHelper::Format(
					"HookD3D12->Hook_CreatePipelineState: original call failed count = %llu result = %s deviceRemovedReason = %s device = %p streamBytes = %llu thread = %lu activePipelineCalls = %u",
					static_cast<unsigned long long>(failureCount),
					StringHelper::FormatHRESULT(result).c_str(),
					StringHelper::FormatHRESULT(removedReason).c_str(),
					device,
					static_cast<unsigned long long>(description ? description->SizeInBytes : 0),
					static_cast<unsigned long>(GetCurrentThreadId()),
					static_cast<unsigned int>(gActivePipelineActivityCount.load(std::memory_order_acquire))));
			}

			return result;
		}

		if (!description || !pipelineState || !*pipelineState)
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->Hook_CreatePipelineState: not succeded, no objects");
			return result;
		}

		CapturePipelineStateStream(description, static_cast<ID3D12PipelineState*>(*pipelineState));
		return result;
	}
}
