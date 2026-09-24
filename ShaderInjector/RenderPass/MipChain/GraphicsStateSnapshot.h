#pragma once

#include <vector>

#include <d3d12.h>

#include "RenderPass/MipChain/DescriptorHeapBinding.h"
#include "RenderPass/MipChain/RootArgumentSnapshot.h"

namespace RenderPassMipChain
{
	//capture the bindings a mip pass must restore before the game continues recording.
	struct GraphicsStateSnapshot
	{
		ID3D12RootSignature* rootSignature = nullptr;
		ID3D12PipelineState* pipelineState = nullptr;
		D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
		std::vector<DescriptorHeapBinding> descriptorHeaps;
		std::vector<RootArgumentSnapshot> rootBindings;
		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> renderTargets;
		D3D12_CPU_DESCRIPTOR_HANDLE depthStencil{};
		std::vector<D3D12_VIEWPORT> viewports;
		std::vector<D3D12_RECT> scissorRectangles;
	};
} //namespace RenderPassMipChain
