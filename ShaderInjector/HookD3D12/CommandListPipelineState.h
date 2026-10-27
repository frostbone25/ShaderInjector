#pragma once

#include <atomic>
#include <cstdint>

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
		//the PSO actually recorded after overrides; a retire only ever frees an injector replacement.
		std::atomic<ID3D12PipelineState*> boundPipelineState = nullptr;
		//bumped on every Reset, so one increment retires every reference captured for this list.
		std::atomic<uint64_t> recordingGeneration = 0;
	};
} //namespace HookD3D12
