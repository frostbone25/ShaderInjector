#include "HookD3D12HookHandlers.h"

#include "GUI/ShaderInjectorGUI.h"
#include "IO/SystemInfoLogger.h"

namespace HookD3D12
{
	HRESULT WINAPI Hook_CreateDeviceD3D12(IUnknown* adapter, D3D_FEATURE_LEVEL minimumFeatureLevel, REFIID interfaceId, void** device)
	{
		return Handle_CreateDeviceD3D12(adapter, minimumFeatureLevel, interfaceId, device);
	}

	HRESULT WINAPI Handle_CreateDeviceD3D12(IUnknown* adapter, D3D_FEATURE_LEVEL minimumFeatureLevel, REFIID interfaceId, void** deviceObject)
	{
		HRESULT createDeviceResult = Original_CreateDeviceD3D12(adapter, minimumFeatureLevel, interfaceId, deviceObject);

		if (SUCCEEDED(createDeviceResult) && deviceObject && *deviceObject)
		{
			ID3D12Device* device = nullptr;
			IUnknown* unknown = reinterpret_cast<IUnknown*>(*deviceObject);

			if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&device))))
			{
				InstallPipelineHooksForDevice(device);
				InstallRenderPassResourceHooksForDevice(device);
				SystemInfoLogger::LogD3D12DeviceInfo(device);
				ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12Install->Hook_CreateDeviceD3D12: D3D12CreateDevice captured device and installed pipeline hooks");
				device->Release();
			}
		}

		return createDeviceResult;
	}
}
