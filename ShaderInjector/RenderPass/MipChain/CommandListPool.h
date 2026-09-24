#pragma once

#include <memory>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "RenderPass/MipChain/ExecutionSlot.h"

namespace RenderPassMipChain
{
	//recycle execution slots on one command list after its submissions retire.
	struct CommandListPool
	{
		Microsoft::WRL::ComPtr<ID3D12Device> device;
		std::vector<std::unique_ptr<ExecutionSlot>> slots;
		std::vector<ExecutionSlot*> recordedSlots;
	};
} //namespace RenderPassMipChain
