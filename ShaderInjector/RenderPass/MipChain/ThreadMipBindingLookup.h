#pragma once

#include <d3d12.h>

#include "RenderPass/RenderPass.h"
#include "RenderPass/RenderPassResourceRegistry.h"

namespace RenderPassMipChain
{
	//cache where a pass's source texture binds in one root signature.
	struct ThreadMipBindingLookup
	{
		const RenderPass::RenderPassDisk* renderPass = nullptr;
		ID3D12RootSignature* rootSignature = nullptr;
		bool computePipeline = false;
		RenderPassResourceRegistry::DescriptorBindingLocation location;
		bool found = false;
	};
} //namespace RenderPassMipChain
