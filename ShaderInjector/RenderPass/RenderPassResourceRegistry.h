#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>

#include "RenderPass.h"

namespace RenderPassResourceRegistry
{
	struct DescriptorTableLayout
	{
		UINT rootParameterIndex = UINT32_MAX;
		D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
		UINT descriptorCount = 0;
		bool containsUnboundedRange = false;
	};

	struct DescriptorBindingLocation
	{
		UINT rootParameterIndex = UINT32_MAX;
		UINT tableOffset = UINT32_MAX;
		UINT shaderRegister = UINT32_MAX;
		UINT registerSpace = UINT32_MAX;
		D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
		UINT descriptorCount = 0;
		bool tableContainsUnboundedRange = false;
	};

	void RegisterRootSignature(
		ID3D12RootSignature* rootSignature,
		const void* serializedRootSignature,
		SIZE_T serializedRootSignatureSize);
	void RegisterResource(ID3D12Resource* resource);

	void RegisterConstantBufferView(
		const D3D12_CONSTANT_BUFFER_VIEW_DESC* description,
		D3D12_CPU_DESCRIPTOR_HANDLE destination);
	void RegisterShaderResourceView(
		ID3D12Resource* resource,
		const D3D12_SHADER_RESOURCE_VIEW_DESC* description,
		D3D12_CPU_DESCRIPTOR_HANDLE destination,
		bool trackAllShaderResourceViews);
	void RegisterUnorderedAccessView(
		ID3D12Resource* resource,
		ID3D12Resource* counterResource,
		const D3D12_UNORDERED_ACCESS_VIEW_DESC* description,
		D3D12_CPU_DESCRIPTOR_HANDLE destination);
	void RegisterRenderTargetView(
		ID3D12Resource* resource,
		const D3D12_RENDER_TARGET_VIEW_DESC* description,
		D3D12_CPU_DESCRIPTOR_HANDLE destination);
	void RegisterDepthStencilView(
		ID3D12Resource* resource,
		const D3D12_DEPTH_STENCIL_VIEW_DESC* description,
		D3D12_CPU_DESCRIPTOR_HANDLE destination);
	void RegisterSampler(D3D12_CPU_DESCRIPTOR_HANDLE destination);

	void CopyDescriptors(
		UINT destinationRangeCount,
		const D3D12_CPU_DESCRIPTOR_HANDLE* destinationRangeStarts,
		const UINT* destinationRangeSizes,
		UINT sourceRangeCount,
		const D3D12_CPU_DESCRIPTOR_HANDLE* sourceRangeStarts,
		const UINT* sourceRangeSizes,
		UINT descriptorIncrementSize);
	void CopyDescriptorsSimple(
		UINT descriptorCount,
		D3D12_CPU_DESCRIPTOR_HANDLE destinationStart,
		D3D12_CPU_DESCRIPTOR_HANDLE sourceStart,
		UINT descriptorIncrementSize);

	bool ResolveDescriptor(
		D3D12_CPU_DESCRIPTOR_HANDLE descriptor,
		RenderPass::ResourceBindingDiagnostic& outBinding);
	bool ResolveGpuVirtualAddress(
		D3D12_GPU_VIRTUAL_ADDRESS gpuAddress,
		RenderPass::ResourceBindingDiagnostic& outBinding);
	void AnnotateRootDescriptor(
		ID3D12RootSignature* rootSignature,
		UINT rootParameterIndex,
		RenderPass::ResourceBindingDiagnostic& binding);
	void ResolveDescriptorTable(
		ID3D12RootSignature* rootSignature,
		UINT rootParameterIndex,
		D3D12_CPU_DESCRIPTOR_HANDLE tableStart,
		uint32_t descriptorHeapType,
		uint32_t firstDescriptorIndex,
		UINT descriptorIncrementSize,
		uint32_t maximumDescriptors,
		const std::string& pipeline,
		std::vector<RenderPass::ResourceBindingDiagnostic>& outBindings);
	bool GetDescriptorTableLayouts(
		ID3D12RootSignature* rootSignature,
		UINT maximumUnboundedDescriptors,
		std::vector<DescriptorTableLayout>& outLayouts);
	bool FindDescriptorBinding(
		ID3D12RootSignature* rootSignature,
		D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
		UINT shaderRegister,
		UINT registerSpace,
		UINT maximumUnboundedDescriptors,
		DescriptorBindingLocation& outLocation);
	bool FindUniqueDescriptorBindingByShaderRegister(
		ID3D12RootSignature* rootSignature,
		D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
		UINT shaderRegister,
		UINT maximumUnboundedDescriptors,
		DescriptorBindingLocation& outLocation);
}
