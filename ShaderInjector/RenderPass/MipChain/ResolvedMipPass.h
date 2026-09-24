#pragma once

#include <cstddef>

#include <d3d12.h>
#include <wrl/client.h>

#include "RenderPass/MipChain/MipTextureResources.h"
#include "RenderPass/RenderPass.h"
#include "RenderPass/RenderPassResourceRegistry.h"

namespace RenderPassMipChain
{
	//keep the selected source descriptor and destination allocation for one mip pass.
	struct ResolvedMipPass
	{
		const RenderPass::RenderPassDisk* renderPass = nullptr;
		size_t resultIndex = 0;
		RenderPassResourceRegistry::DescriptorBindingLocation bindingLocation;
		D3D12_CPU_DESCRIPTOR_HANDLE sourceDescriptor{};
		RenderPass::ResourceBindingDiagnostic sourceMetadata;
		Microsoft::WRL::ComPtr<ID3D12Resource> sourceResource;
		MipTextureResources* resources = nullptr;
	};
} //namespace RenderPassMipChain
