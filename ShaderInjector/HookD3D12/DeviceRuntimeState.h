#pragma once

#include <atomic>
#include <d3d12.h>
#include <wrl/client.h>
#include "D3D12PipelineInfo.h"

namespace HookD3D12
{
	struct DeviceRuntimeState
	{
		Microsoft::WRL::ComPtr<IUnknown> identity;
		Microsoft::WRL::ComPtr<ID3D12Device> device;
		Microsoft::WRL::ComPtr<ID3D12Device2> streamDevice;
		D3D12PipelineInfo pipelineInfo;
		Microsoft::WRL::ComPtr<ID3D12Fence> overlayFence;
		HANDLE overlayFenceEvent = nullptr;
		HANDLE removalEvent = nullptr;
		std::atomic<HRESULT> removalReason = S_OK;

		~DeviceRuntimeState();
	};
}
