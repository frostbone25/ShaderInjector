#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "RenderPass/RenderPassResourceRegistry.h"
#include "ShaderResource/Runtime/ActiveTable.h"
#include "ShaderResource/Runtime/DescriptorExecutionSlot.h"
#include "ShaderResource/Runtime/RootTableRestore.h"
#include "ShaderResource/Runtime/TextureGPU.h"

namespace ShaderResourceRuntime
{
	//keep per-command-list descriptor copies, resolved textures, and restore state together.
	struct CommandListSlot
	{
		Microsoft::WRL::ComPtr<ID3D12Device> device;
		UINT descriptorIncrementSize = 0;
		UINT samplerDescriptorIncrementSize = 0;
		ID3D12RootSignature* cachedRootSignature = nullptr;
		uint32_t cachedMaximumTrackedDescriptors = 0;
		std::vector<RenderPassResourceRegistry::DescriptorTableLayout> layouts;
		std::vector<ActiveTable> activeTables;
		std::unordered_map<uint64_t, std::vector<RenderPassResourceRegistry::DescriptorBindingLocation>> graphicsBindingLocations;
		std::unordered_map<uint64_t, std::vector<RenderPassResourceRegistry::DescriptorBindingLocation>> computeBindingLocations;
		std::unordered_map<std::string, TextureGPU*> resolvedTextures;
		std::vector<ID3D12DescriptorHeap*> restoreHeaps;
		std::vector<RootTableRestore> restoreRootTables;
		std::vector<std::unique_ptr<DescriptorExecutionSlot>> executionSlots;
		std::vector<DescriptorExecutionSlot*> recordedExecutionSlots;
		DescriptorExecutionSlot* currentExecutionSlot = nullptr;
		bool pendingRestore = false;
	};
} //namespace ShaderResourceRuntime
