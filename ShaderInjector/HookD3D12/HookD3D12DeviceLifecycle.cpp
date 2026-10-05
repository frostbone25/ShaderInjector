#include "HookD3D12DeviceLifecycle.h"

#include <memory>
#include <mutex>
#include <unordered_map>

#include "HookD3D12.h"
#include "D3D12/HookD3D12RuntimeState.h"
#include "IO/ShaderInjectorIO.h"
#include "StringHelper.h"

namespace HookD3D12
{
	static std::mutex gDeviceRuntimeMutex;
	static std::unordered_map<IUnknown*, std::unique_ptr<DeviceRuntimeState>> gDeviceRuntimeStates;
	static DeviceRuntimeState* gOverlayDeviceRuntimeState = nullptr;

	DeviceRuntimeState::~DeviceRuntimeState()
	{
		//destroy the fence before closing handles registered for its completion notifications.
		overlayFence.Reset();

		if (overlayFenceEvent)
			CloseHandle(overlayFenceEvent);

		if (removalEvent)
			CloseHandle(removalEvent);
	}

	DeviceRuntimeState* RegisterDevice(ID3D12Device* device)
	{
		if (!device)
			return nullptr;

		Microsoft::WRL::ComPtr<IUnknown> identity;

		if (FAILED(device->QueryInterface(IID_PPV_ARGS(&identity))))
			return nullptr;

		std::lock_guard<std::mutex> deviceLock(gDeviceRuntimeMutex);
		const auto existingDevice = gDeviceRuntimeStates.find(identity.Get());

		if (existingDevice != gDeviceRuntimeStates.end())
			return existingDevice->second.get();

		auto deviceState = std::make_unique<DeviceRuntimeState>();
		deviceState->identity = identity;
		deviceState->device = device;
		device->QueryInterface(IID_PPV_ARGS(&deviceState->streamDevice));
		GatherD3D12PipelineInfo(nullptr, device, nullptr, deviceState->pipelineInfo);
		deviceState->removalReason.store(device->GetDeviceRemovedReason(), std::memory_order_release);

		//the fence and wait handles belong to this device, not to its current back buffers.
		const HRESULT fenceResult = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&deviceState->overlayFence));

		if (SUCCEEDED(fenceResult))
		{
			deviceState->overlayFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			deviceState->removalEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

			//D3D12 completes all fences at UINT64_MAX on removal, even when no work is pending.
			if (deviceState->removalEvent)
			{
				const HRESULT eventResult = deviceState->overlayFence->SetEventOnCompletion(UINT64_MAX, deviceState->removalEvent);

				if (FAILED(eventResult))
				{
					CloseHandle(deviceState->removalEvent);
					deviceState->removalEvent = nullptr;
				}
			}
		}
		else
			ShaderInjectorIO::WriteToLogFileError("HookD3D12->RegisterDevice: fence creation failed " + StringHelper::FormatHRESULT(fenceResult));

		DeviceRuntimeState* registeredState = deviceState.get();
		gDeviceRuntimeStates.emplace(identity.Get(), std::move(deviceState));

		ShaderInjectorIO::LogD3D12DeviceInfo(device);
		ShaderInjectorIO::WriteToLogFile(StringHelper::Format("HookD3D12->RegisterDevice: prepared device=%p streamInterface=%d", device, registeredState->streamDevice.Get() != nullptr));

		return registeredState;
	}

	bool AdoptOverlayDeviceResources(ID3D12Device* device)
	{
		if (gOverlayDeviceRuntimeState && gOverlayDeviceRuntimeState->device.Get() == device && gOverlayFence && gFenceEvent)
			return IsOverlayDeviceAvailable();

		//never pair a second device with the first device's still-live overlay fence and backend.
		if (gOverlayDeviceRuntimeState && gOverlayDeviceRuntimeState->device.Get() != device)
			return false;

		DeviceRuntimeState* deviceState = RegisterDevice(device);

		if (!deviceState || FAILED(deviceState->removalReason.load(std::memory_order_acquire)) || !deviceState->overlayFence || !deviceState->overlayFenceEvent)
			return false;

		//the overlay keeps its own references and handle so its existing teardown stays independent.
		if (!gDevice2 && deviceState->streamDevice)
		{
			gDevice2 = deviceState->streamDevice.Get();
			gDevice2->AddRef();
		}

		if (!gOverlayFence)
		{
			gOverlayFence = deviceState->overlayFence.Get();
			gOverlayFence->AddRef();
		}

		if (!gFenceEvent)
		{
			if (!DuplicateHandle(GetCurrentProcess(), deviceState->overlayFenceEvent, GetCurrentProcess(), &gFenceEvent, 0, FALSE, DUPLICATE_SAME_ACCESS))
				return false;
		}

		gOverlayDeviceRuntimeState = deviceState;
		return true;
	}

	bool IsOverlayDeviceAvailable()
	{
		return gOverlayDeviceRuntimeState && SUCCEEDED(gOverlayDeviceRuntimeState->removalReason.load(std::memory_order_acquire));
	}

	void NotifyDeviceRemoved(ID3D12Device* device, HRESULT reason)
	{
		if (!device || SUCCEEDED(reason))
			return;

		DeviceRuntimeState* deviceState = RegisterDevice(device);

		if (!deviceState)
			return;

		HRESULT previousReason = S_OK;

		if (deviceState->removalReason.compare_exchange_strong(previousReason, reason, std::memory_order_acq_rel))
			ShaderInjectorIO::WriteToLogFileError(StringHelper::Format("HookD3D12->NotifyDeviceRemoved: device=%p reason=%s; injection stopped for this device", device, StringHelper::FormatHRESULT(reason).c_str()));
	}

	void PollDeviceRemovals()
	{
		std::lock_guard<std::mutex> deviceLock(gDeviceRuntimeMutex);

		for (const auto& deviceEntry : gDeviceRuntimeStates)
		{
			DeviceRuntimeState& deviceState = *deviceEntry.second;

			if (FAILED(deviceState.removalReason.load(std::memory_order_acquire)) || !deviceState.removalEvent || WaitForSingleObject(deviceState.removalEvent, 0) != WAIT_OBJECT_0)
				continue;

			const HRESULT reason = deviceState.device->GetDeviceRemovedReason();

			if (SUCCEEDED(reason))
				continue;

			deviceState.removalReason.store(reason, std::memory_order_release);
			ShaderInjectorIO::WriteToLogFileError(StringHelper::Format("HookD3D12->PollDeviceRemovals: device=%p reason=%s; injection stopped for this device", deviceState.device.Get(), StringHelper::FormatHRESULT(reason).c_str()));
		}
	}

	void ReleaseDeviceRuntimeStates()
	{
		//called only after injection has stopped and overlay GPU resources have been released.
		std::lock_guard<std::mutex> deviceLock(gDeviceRuntimeMutex);
		gOverlayDeviceRuntimeState = nullptr;
		gDeviceRuntimeStates.clear();
	}
}
