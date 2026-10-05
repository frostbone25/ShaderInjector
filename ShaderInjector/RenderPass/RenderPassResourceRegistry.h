#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>

#include "RenderPass.h"
#include "RenderPass/Registry/RegistryStatistics.h"
#include "RenderPass/Registry/DescriptorTableLayout.h"
#include "RenderPass/Registry/DescriptorBindingLocation.h"

namespace RenderPassResourceRegistry
{
	void RegisterRootSignature(
		ID3D12RootSignature* rootSignature,
		const void* serializedRootSignature,
		SIZE_T serializedRootSignatureSize);
	void RegisterDescriptorHeap(
		ID3D12DescriptorHeap* descriptorHeap,
		UINT descriptorIncrementSize = 0,
		bool newlyCreated = false);
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

	//mirror direct writes as well as copies; preserve the game's exact view description and null/default views.
	bool IsInsideDescriptorMirrorOperation();
	bool IsDescriptorMirroringActive();
	void MirrorConstantBufferView(ID3D12Device* device, const D3D12_CONSTANT_BUFFER_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination);
	void MirrorShaderResourceView(ID3D12Device* device, ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination);
	void MirrorUnorderedAccessView(ID3D12Device* device, ID3D12Resource* resource, ID3D12Resource* counterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination);
	void MirrorSampler(ID3D12Device* device, const D3D12_SAMPLER_DESC* description, D3D12_CPU_DESCRIPTOR_HANDLE destination);

	//clone complete tables from CPU-only mirrors, retaining owners and read locks through the native copy.
	bool CopyDescriptorTables(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE destination, UINT destinationDescriptorCount, UINT sourceRangeCount, const D3D12_CPU_DESCRIPTOR_HANDLE* originalSources, const UINT* sourceSizes, D3D12_DESCRIPTOR_HEAP_TYPE heapType);

	bool CopyDescriptors(
		UINT destinationRangeCount,
		const D3D12_CPU_DESCRIPTOR_HANDLE* destinationRangeStarts,
		const UINT* destinationRangeSizes,
		UINT sourceRangeCount,
		const D3D12_CPU_DESCRIPTOR_HANDLE* sourceRangeStarts,
		const UINT* sourceRangeSizes,
		UINT descriptorIncrementSize,
		ID3D12Device* device = nullptr,
		bool trackMetadata = true);
	bool CopyDescriptorsSimple(
		UINT descriptorCount,
		D3D12_CPU_DESCRIPTOR_HANDLE destinationStart,
		D3D12_CPU_DESCRIPTOR_HANDLE sourceStart,
		UINT descriptorIncrementSize,
		ID3D12Device* device = nullptr,
		bool trackMetadata = true);

	bool ResolveDescriptor(
		D3D12_CPU_DESCRIPTOR_HANDLE descriptor,
		RenderPass::ResourceBindingDiagnostic& outBinding);
	UINT CountContiguousDescriptors(
		D3D12_CPU_DESCRIPTOR_HANDLE firstDescriptor,
		UINT descriptorIncrementSize,
		UINT maximumDescriptors);
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
		D3D12_SHADER_VISIBILITY shaderVisibility,
		DescriptorBindingLocation& outLocation);
	bool GetDescriptorBindingCandidates(
		ID3D12RootSignature* rootSignature,
		D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
		UINT shaderRegister,
		UINT registerSpace,
		UINT maximumUnboundedDescriptors,
		D3D12_SHADER_VISIBILITY shaderVisibility,
		std::vector<DescriptorBindingLocation>& outLocations);
	bool FindUniqueDescriptorBindingByShaderRegister(
		ID3D12RootSignature* rootSignature,
		D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
		UINT shaderRegister,
		UINT maximumUnboundedDescriptors,
		D3D12_SHADER_VISIBILITY shaderVisibility,
		DescriptorBindingLocation& outLocation);
	RegistryStatistics GetStatistics();
} //namespace RenderPassResourceRegistry
