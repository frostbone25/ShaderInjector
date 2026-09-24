#pragma once

#include <atomic>

#include <d3d12.h>

namespace HookD3D12
{
	struct CommandListPipelineState
	{
		std::atomic<ID3D12RootSignature*> graphicsRootSignature = nullptr;
		std::atomic<ID3D12RootSignature*> computeRootSignature = nullptr;
		std::atomic<ID3D12PipelineState*> pipelineState = nullptr;
	};
} //namespace HookD3D12
