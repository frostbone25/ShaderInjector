#pragma once

#include <atomic>

#include <d3d12.h>

#include "../Globals.h"

namespace HookD3D12
{
	struct DeviceRuntimeState;

	struct CommandListPipelineState
	{
		bool injectorEnabledForRecording = Globals::gShaderInjectorEnabled.load(std::memory_order_acquire);
		DeviceRuntimeState* deviceRuntimeState = nullptr;
		std::atomic<ID3D12RootSignature*> graphicsRootSignature = nullptr;
		std::atomic<ID3D12RootSignature*> computeRootSignature = nullptr;
		std::atomic<ID3D12PipelineState*> pipelineState = nullptr;
	};
} //namespace HookD3D12
