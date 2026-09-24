#pragma once

#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "ShaderResource/Runtime/DescriptorHeapPage.h"

namespace ShaderResourceRuntime
{
	//hold descriptor pages until this command list's GPU submission retires.
	struct DescriptorExecutionSlot
	{
		std::vector<DescriptorHeapPage> pages;
		std::vector<DescriptorHeapPage> samplerPages;
		Microsoft::WRL::ComPtr<ID3D12Fence> retirementFence;
		UINT64 retirementFenceValue = 0;
		bool recorded = false;
		bool submitted = false;
		bool retirementBlocked = false;
	};
} //namespace ShaderResourceRuntime
