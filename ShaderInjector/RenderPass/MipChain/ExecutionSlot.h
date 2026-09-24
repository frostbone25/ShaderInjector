#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "RenderPass/MipChain/MipTextureResources.h"
#include "RenderPass/RecordedTextureState.h"
#include "RenderPass/RenderPassResourceRegistry.h"

namespace RenderPassMipChain
{
	//keep a pass's textures and descriptors stable while a command list is recorded.
	struct ExecutionSlot
	{
		//key resources by pass so changing execution order does not recreate textures or descriptors.
		std::unordered_map<std::string, MipTextureResources> mipTexturesByRenderPassId;
		std::vector<RecordedTextureState> recordedTextureStates;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> nativeDescriptorHeap;
		UINT nativeDescriptorCapacity = 0;
		ID3D12RootSignature* cachedLayoutRootSignature = nullptr;
		UINT cachedLayoutMaximumDescriptors = 0;
		std::vector<RenderPassResourceRegistry::DescriptorTableLayout> cachedDescriptorTableLayouts;
		Microsoft::WRL::ComPtr<ID3D12Fence> retirementFence;
		UINT64 retirementFenceValue = 0;
		bool recorded = false;
		bool submitted = false;
		bool retirementBlocked = false;
	};
} //namespace RenderPassMipChain
