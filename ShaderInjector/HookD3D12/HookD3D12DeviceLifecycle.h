#pragma once

#include "DeviceRuntimeState.h"

namespace HookD3D12
{
	//prepare device-owned interfaces and synchronization once, independently of any swap chain.
	DeviceRuntimeState* RegisterDevice(ID3D12Device* device);

	//select the prepared device resources for the single active overlay swap chain.
	bool AdoptOverlayDeviceResources(ID3D12Device* device);
	bool IsOverlayDeviceAvailable();

	//a removal fence wakes without a polling thread; inspect its event during frame maintenance.
	void PollDeviceRemovals();
	void NotifyDeviceRemoved(ID3D12Device* device, HRESULT reason);
	void ReleaseDeviceRuntimeStates();
}
