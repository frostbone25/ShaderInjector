#pragma once

#include <d3d12.h>

namespace SystemInfoLogger
{
	void LogProcessAndSystemInfo();
	void LogD3D12DeviceInfo(ID3D12Device* device);
}
