#pragma once

#include <cstdint>
#include <vector>

#include <d3d12.h>

#include "Enum/RenderPassGameResourceViewType.h"
#include "RenderPass/RenderPass.h"
#include "RenderPass/RenderPassResourceRegistry.h"

namespace RenderPassRuntime
{
	//cache matching game descriptors for one pass and root signature on this thread.
	struct ThreadGameTextureBindingLookup
	{
		const RenderPass::RenderPassDisk* renderPass = nullptr;
		ID3D12RootSignature* rootSignature = nullptr;
		RenderPass::GameResourceViewType viewType = RenderPass::GameResourceViewType::UnorderedAccess;
		uint32_t shaderRegister = 0;
		uint32_t registerSpace = 0;
		std::vector<RenderPassResourceRegistry::DescriptorBindingLocation> locations;
	};
} //namespace RenderPassRuntime
