#include "RenderPassResourceRegistry.h"

#include <algorithm>
#include <limits>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

namespace RenderPassResourceRegistry
{
	namespace
	{
		struct DescriptorRangeLayout
		{
			D3D12_DESCRIPTOR_RANGE_TYPE type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			UINT descriptorCount = 0;
			UINT baseShaderRegister = 0;
			UINT registerSpace = 0;
			UINT tableOffset = 0;
		};

		struct RootParameterLayout
		{
			D3D12_ROOT_PARAMETER_TYPE type = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			UINT shaderRegister = UINT32_MAX;
			UINT registerSpace = UINT32_MAX;
			std::vector<DescriptorRangeLayout> ranges;
		};

		struct RootSignatureLayout
		{
			std::vector<RootParameterLayout> parameters;
		};

		std::shared_mutex gDescriptorMutex;
		std::shared_mutex gResourceMutex;
		std::shared_mutex gRootSignatureMutex;
		std::map<SIZE_T, RenderPass::ResourceBindingDiagnostic> gDescriptors;
		std::map<uint64_t, RenderPass::ResourceBindingDiagnostic> gResourcesByGpuAddress;
		std::unordered_map<ID3D12RootSignature*, RootSignatureLayout> gRootSignatures;

		struct PendingDescriptorCopy
		{
			SIZE_T destination = 0;
			RenderPass::ResourceBindingDiagnostic binding;
		};

		struct DescriptorCopySegment
		{
			SIZE_T destinationStart = 0;
			SIZE_T sourceStart = 0;
			UINT descriptorCount = 0;
		};

		const char* DescriptorRangeTypeName(D3D12_DESCRIPTOR_RANGE_TYPE type)
		{
			switch (type)
			{
				case D3D12_DESCRIPTOR_RANGE_TYPE_SRV: return "SRV";
				case D3D12_DESCRIPTOR_RANGE_TYPE_UAV: return "UAV";
				case D3D12_DESCRIPTOR_RANGE_TYPE_CBV: return "CBV";
				case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER: return "Sampler";
				default: return "Descriptor";
			}
		}

		void FillResourceMetadata(
			ID3D12Resource* resource,
			RenderPass::ResourceBindingDiagnostic& binding)
		{
			if (!resource)
				return;

			const D3D12_RESOURCE_DESC description = resource->GetDesc();
			binding.resourcePointer = reinterpret_cast<uint64_t>(resource);
			binding.resourceDimension = static_cast<uint32_t>(description.Dimension);
			binding.resourceWidth = description.Width;
			binding.resourceHeight = description.Height;
			binding.resourceDepthOrArraySize = description.DepthOrArraySize;
			binding.resourceMipLevels = description.MipLevels;
			binding.resourceFormat = static_cast<uint32_t>(description.Format);
			binding.resourceSampleCount = description.SampleDesc.Count;
			binding.resourceSampleQuality = description.SampleDesc.Quality;
			if (description.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
			{
				binding.gpuAddress = resource->GetGPUVirtualAddress();
				binding.bufferSize = description.Width;
			}
		}

		bool ResolveGpuVirtualAddressLocked(
			D3D12_GPU_VIRTUAL_ADDRESS gpuAddress,
			RenderPass::ResourceBindingDiagnostic& outBinding)
		{
			if (!gpuAddress || gResourcesByGpuAddress.empty())
				return false;

			auto resourceIt = gResourcesByGpuAddress.upper_bound(gpuAddress);
			if (resourceIt == gResourcesByGpuAddress.begin())
				return false;
			--resourceIt;

			const uint64_t resourceStart = resourceIt->first;
			const uint64_t resourceSize = resourceIt->second.bufferSize;
			if (!resourceSize || gpuAddress < resourceStart || gpuAddress - resourceStart >= resourceSize)
				return false;

			outBinding = resourceIt->second;
			outBinding.gpuAddress = gpuAddress;
			outBinding.bufferOffset = gpuAddress - resourceStart;
			outBinding.bufferSize = resourceSize - outBinding.bufferOffset;
			return true;
		}

		void StoreDescriptor(
			D3D12_CPU_DESCRIPTOR_HANDLE destination,
			RenderPass::ResourceBindingDiagnostic binding)
		{
			binding.cpuDescriptorHandle = destination.ptr;
			std::unique_lock<std::shared_mutex> lock(gDescriptorMutex);
			gDescriptors[destination.ptr] = std::move(binding);
		}

		void EraseDescriptorIfTracked(D3D12_CPU_DESCRIPTOR_HANDLE destination)
		{
			{
				std::shared_lock<std::shared_mutex> lock(gDescriptorMutex);
				if (gDescriptors.find(destination.ptr) == gDescriptors.end())
					return;
			}

			std::unique_lock<std::shared_mutex> lock(gDescriptorMutex);
			gDescriptors.erase(destination.ptr);
		}

		void FillShaderResourceViewMetadata(
			const D3D12_SHADER_RESOURCE_VIEW_DESC* description,
			RenderPass::ResourceBindingDiagnostic& binding)
		{
			if (!description)
				return;

			binding.resourceFormat = static_cast<uint32_t>(description->Format);
			binding.descriptorViewDimension = static_cast<uint32_t>(description->ViewDimension);
			binding.descriptorShader4ComponentMapping = description->Shader4ComponentMapping;
			switch (description->ViewDimension)
			{
				case D3D12_SRV_DIMENSION_TEXTURE1D:
					binding.descriptorMostDetailedMip = description->Texture1D.MostDetailedMip;
					binding.descriptorMipLevels = description->Texture1D.MipLevels;
					break;
				case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
					binding.descriptorMostDetailedMip = description->Texture1DArray.MostDetailedMip;
					binding.descriptorMipLevels = description->Texture1DArray.MipLevels;
					break;
				case D3D12_SRV_DIMENSION_TEXTURE2D:
					binding.descriptorMostDetailedMip = description->Texture2D.MostDetailedMip;
					binding.descriptorMipLevels = description->Texture2D.MipLevels;
					binding.descriptorPlaneSlice = description->Texture2D.PlaneSlice;
					binding.descriptorResourceMinLodClamp = description->Texture2D.ResourceMinLODClamp;
					break;
				case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
					binding.descriptorMostDetailedMip = description->Texture2DArray.MostDetailedMip;
					binding.descriptorMipLevels = description->Texture2DArray.MipLevels;
					break;
				case D3D12_SRV_DIMENSION_TEXTURE3D:
					binding.descriptorMostDetailedMip = description->Texture3D.MostDetailedMip;
					binding.descriptorMipLevels = description->Texture3D.MipLevels;
					break;
				case D3D12_SRV_DIMENSION_TEXTURECUBE:
					binding.descriptorMostDetailedMip = description->TextureCube.MostDetailedMip;
					binding.descriptorMipLevels = description->TextureCube.MipLevels;
					break;
				case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
					binding.descriptorMostDetailedMip = description->TextureCubeArray.MostDetailedMip;
					binding.descriptorMipLevels = description->TextureCubeArray.MipLevels;
					break;
				default:
					break;
			}
			if (description->ViewDimension == D3D12_SRV_DIMENSION_BUFFER)
			{
				binding.firstElement = description->Buffer.FirstElement;
				binding.elementCount = description->Buffer.NumElements;
				binding.structureByteStride = description->Buffer.StructureByteStride;
			}
		}

		void FillUnorderedAccessViewMetadata(
			const D3D12_UNORDERED_ACCESS_VIEW_DESC* description,
			RenderPass::ResourceBindingDiagnostic& binding)
		{
			if (!description)
				return;

			binding.resourceFormat = static_cast<uint32_t>(description->Format);
			if (description->ViewDimension == D3D12_UAV_DIMENSION_BUFFER)
			{
				binding.firstElement = description->Buffer.FirstElement;
				binding.elementCount = description->Buffer.NumElements;
				binding.structureByteStride = description->Buffer.StructureByteStride;
			}
		}

		SIZE_T DescriptorRangeEnd(
			SIZE_T rangeStart,
			UINT descriptorCount,
			UINT descriptorIncrementSize)
		{
			const SIZE_T maximumValue = (std::numeric_limits<SIZE_T>::max)();
			if (descriptorCount > (maximumValue - rangeStart) / descriptorIncrementSize)
				return maximumValue;
			return rangeStart + static_cast<SIZE_T>(descriptorCount) * descriptorIncrementSize;
		}

		void QueueTrackedDescriptorCopiesLocked(
			const DescriptorCopySegment& segment,
			UINT descriptorIncrementSize,
			std::vector<PendingDescriptorCopy>& pendingCopies)
		{
			if (!segment.descriptorCount)
				return;

			const SIZE_T sourceEnd = DescriptorRangeEnd(
				segment.sourceStart,
				segment.descriptorCount,
				descriptorIncrementSize);
			for (auto sourceIt = gDescriptors.lower_bound(segment.sourceStart);
				sourceIt != gDescriptors.end() && sourceIt->first < sourceEnd;
				++sourceIt)
			{
				const SIZE_T sourceOffset = sourceIt->first - segment.sourceStart;
				if (sourceOffset % descriptorIncrementSize != 0)
					continue;

				const SIZE_T descriptorIndex = sourceOffset / descriptorIncrementSize;
				if (descriptorIndex >= segment.descriptorCount)
					continue;

				PendingDescriptorCopy copy{};
				copy.destination = segment.destinationStart + descriptorIndex * descriptorIncrementSize;
				copy.binding = sourceIt->second;
				copy.binding.cpuDescriptorHandle = copy.destination;
				pendingCopies.push_back(std::move(copy));
			}
		}

		void EraseTrackedDescriptorRangeLocked(
			const DescriptorCopySegment& segment,
			UINT descriptorIncrementSize)
		{
			if (!segment.descriptorCount)
				return;
			const SIZE_T destinationEnd = DescriptorRangeEnd(
				segment.destinationStart,
				segment.descriptorCount,
				descriptorIncrementSize);
			auto destinationIt = gDescriptors.lower_bound(segment.destinationStart);
			while (destinationIt != gDescriptors.end() && destinationIt->first < destinationEnd)
				destinationIt = gDescriptors.erase(destinationIt);
		}

		bool HasTrackedDescriptorInDestinationRangeLocked(
			const DescriptorCopySegment& segment,
			UINT descriptorIncrementSize)
		{
			if (!segment.descriptorCount)
				return false;
			const SIZE_T destinationEnd = DescriptorRangeEnd(
				segment.destinationStart,
				segment.descriptorCount,
				descriptorIncrementSize);
			const auto destinationIt = gDescriptors.lower_bound(segment.destinationStart);
			return destinationIt != gDescriptors.end() && destinationIt->first < destinationEnd;
		}

		void ApplyPendingDescriptorCopiesLocked(std::vector<PendingDescriptorCopy>& pendingCopies)
		{
			for (PendingDescriptorCopy& copy : pendingCopies)
				gDescriptors[copy.destination] = std::move(copy.binding);
		}
	}

	void RegisterRootSignature(
		ID3D12RootSignature* rootSignature,
		const void* serializedRootSignature,
		SIZE_T serializedRootSignatureSize)
	{
		if (!rootSignature || !serializedRootSignature || !serializedRootSignatureSize)
			return;

		{
			std::shared_lock<std::shared_mutex> lock(gRootSignatureMutex);
			if (gRootSignatures.find(rootSignature) != gRootSignatures.end())
				return;
		}

		ID3D12VersionedRootSignatureDeserializer* deserializer = nullptr;
		if (FAILED(D3D12CreateVersionedRootSignatureDeserializer(
			serializedRootSignature,
			serializedRootSignatureSize,
			IID_PPV_ARGS(&deserializer))) || !deserializer)
		{
			return;
		}

		const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* description =
			deserializer->GetUnconvertedRootSignatureDesc();
		RootSignatureLayout layout{};
		if (description)
		{
			const UINT parameterCount = description->Version == D3D_ROOT_SIGNATURE_VERSION_1_0
				? description->Desc_1_0.NumParameters
				: description->Desc_1_1.NumParameters;
			layout.parameters.resize(parameterCount);

			for (UINT parameterIndex = 0; parameterIndex < parameterCount; ++parameterIndex)
			{
				RootParameterLayout& parameterLayout = layout.parameters[parameterIndex];
				if (description->Version == D3D_ROOT_SIGNATURE_VERSION_1_0)
				{
					const D3D12_ROOT_PARAMETER& parameter = description->Desc_1_0.pParameters[parameterIndex];
					parameterLayout.type = parameter.ParameterType;
					if (parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
					{
						UINT appendedOffset = 0;
						for (UINT rangeIndex = 0; rangeIndex < parameter.DescriptorTable.NumDescriptorRanges; ++rangeIndex)
						{
							const D3D12_DESCRIPTOR_RANGE& range = parameter.DescriptorTable.pDescriptorRanges[rangeIndex];
							const UINT tableOffset = range.OffsetInDescriptorsFromTableStart == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
								? appendedOffset
								: range.OffsetInDescriptorsFromTableStart;
							parameterLayout.ranges.push_back({ range.RangeType, range.NumDescriptors, range.BaseShaderRegister, range.RegisterSpace, tableOffset });
							if (range.NumDescriptors != UINT_MAX)
								appendedOffset = tableOffset + range.NumDescriptors;
						}
					}
					else if (parameter.ParameterType != D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
					{
						parameterLayout.shaderRegister = parameter.Descriptor.ShaderRegister;
						parameterLayout.registerSpace = parameter.Descriptor.RegisterSpace;
					}
				}
				else
				{
					const D3D12_ROOT_PARAMETER1& parameter = description->Desc_1_1.pParameters[parameterIndex];
					parameterLayout.type = parameter.ParameterType;
					if (parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
					{
						UINT appendedOffset = 0;
						for (UINT rangeIndex = 0; rangeIndex < parameter.DescriptorTable.NumDescriptorRanges; ++rangeIndex)
						{
							const D3D12_DESCRIPTOR_RANGE1& range = parameter.DescriptorTable.pDescriptorRanges[rangeIndex];
							const UINT tableOffset = range.OffsetInDescriptorsFromTableStart == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
								? appendedOffset
								: range.OffsetInDescriptorsFromTableStart;
							parameterLayout.ranges.push_back({ range.RangeType, range.NumDescriptors, range.BaseShaderRegister, range.RegisterSpace, tableOffset });
							if (range.NumDescriptors != UINT_MAX)
								appendedOffset = tableOffset + range.NumDescriptors;
						}
					}
					else if (parameter.ParameterType != D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
					{
						parameterLayout.shaderRegister = parameter.Descriptor.ShaderRegister;
						parameterLayout.registerSpace = parameter.Descriptor.RegisterSpace;
					}
				}
			}
		}

		deserializer->Release();
		std::unique_lock<std::shared_mutex> lock(gRootSignatureMutex);
		gRootSignatures[rootSignature] = std::move(layout);
	}

	void RegisterResource(ID3D12Resource* resource)
	{
		if (!resource)
			return;

		RenderPass::ResourceBindingDiagnostic resourceMetadata{};
		FillResourceMetadata(resource, resourceMetadata);
		if (!resourceMetadata.gpuAddress || !resourceMetadata.bufferSize)
			return;

		std::unique_lock<std::shared_mutex> lock(gResourceMutex);
		gResourcesByGpuAddress[resourceMetadata.gpuAddress] = std::move(resourceMetadata);
	}

	void RegisterConstantBufferView(
		const D3D12_CONSTANT_BUFFER_VIEW_DESC* description,
		D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		RenderPass::ResourceBindingDiagnostic binding{};
		binding.bindingType = "CBV";
		if (description)
		{
			std::shared_lock<std::shared_mutex> lock(gResourceMutex);
			ResolveGpuVirtualAddressLocked(description->BufferLocation, binding);
			binding.bindingType = "CBV";
			binding.gpuAddress = description->BufferLocation;
			binding.bufferSize = description->SizeInBytes;
		}
		StoreDescriptor(destination, std::move(binding));
	}

	void RegisterShaderResourceView(
		ID3D12Resource* resource,
		const D3D12_SHADER_RESOURCE_VIEW_DESC* description,
		D3D12_CPU_DESCRIPTOR_HANDLE destination,
		bool trackAllShaderResourceViews)
	{
		RenderPass::ResourceBindingDiagnostic binding{};
		binding.bindingType = "SRV";
		FillResourceMetadata(resource, binding);
		if (!trackAllShaderResourceViews)
		{
			if (!resource)
			{
				EraseDescriptorIfTracked(destination);
				return;
			}

			const bool supportedTexture =
				binding.resourceDimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
				binding.resourceDepthOrArraySize == 1 &&
				binding.resourceSampleCount == 1;
			const bool supportedView = !description ||
				(description->ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D &&
					description->Texture2D.PlaneSlice == 0);
			if (!supportedTexture || !supportedView)
			{
				EraseDescriptorIfTracked(destination);
				return;
			}
		}

		FillShaderResourceViewMetadata(description, binding);
		StoreDescriptor(destination, std::move(binding));
	}

	void RegisterUnorderedAccessView(
		ID3D12Resource* resource,
		ID3D12Resource* counterResource,
		const D3D12_UNORDERED_ACCESS_VIEW_DESC* description,
		D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		RegisterResource(resource);
		RegisterResource(counterResource);
		RenderPass::ResourceBindingDiagnostic binding{};
		binding.bindingType = "UAV";
		FillResourceMetadata(resource, binding);
		FillUnorderedAccessViewMetadata(description, binding);
		StoreDescriptor(destination, std::move(binding));
	}

	void RegisterRenderTargetView(
		ID3D12Resource* resource,
		const D3D12_RENDER_TARGET_VIEW_DESC* description,
		D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		RenderPass::ResourceBindingDiagnostic binding{};
		binding.bindingType = "RTV";
		FillResourceMetadata(resource, binding);
		if (description)
			binding.resourceFormat = static_cast<uint32_t>(description->Format);
		StoreDescriptor(destination, std::move(binding));
	}

	void RegisterDepthStencilView(
		ID3D12Resource* resource,
		const D3D12_DEPTH_STENCIL_VIEW_DESC* description,
		D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		RenderPass::ResourceBindingDiagnostic binding{};
		binding.bindingType = "DSV";
		FillResourceMetadata(resource, binding);
		if (description)
			binding.resourceFormat = static_cast<uint32_t>(description->Format);
		StoreDescriptor(destination, std::move(binding));
	}

	void RegisterSampler(D3D12_CPU_DESCRIPTOR_HANDLE destination)
	{
		RenderPass::ResourceBindingDiagnostic binding{};
		binding.bindingType = "Sampler";
		StoreDescriptor(destination, std::move(binding));
	}

	void CopyDescriptors(
		UINT destinationRangeCount,
		const D3D12_CPU_DESCRIPTOR_HANDLE* destinationRangeStarts,
		const UINT* destinationRangeSizes,
		UINT sourceRangeCount,
		const D3D12_CPU_DESCRIPTOR_HANDLE* sourceRangeStarts,
		const UINT* sourceRangeSizes,
		UINT descriptorIncrementSize)
	{
		if (!destinationRangeStarts || !sourceRangeStarts || !descriptorIncrementSize)
			return;

		std::vector<DescriptorCopySegment> segments;
		segments.reserve(destinationRangeCount + sourceRangeCount);
		UINT destinationRangeIndex = 0;
		UINT sourceRangeIndex = 0;
		UINT destinationOffset = 0;
		UINT sourceOffset = 0;
		while (destinationRangeIndex < destinationRangeCount && sourceRangeIndex < sourceRangeCount)
		{
			const UINT destinationRangeSize = destinationRangeSizes ? destinationRangeSizes[destinationRangeIndex] : 1;
			const UINT sourceRangeSize = sourceRangeSizes ? sourceRangeSizes[sourceRangeIndex] : 1;
			const UINT copyCount = (std::min)(
				destinationRangeSize - destinationOffset,
				sourceRangeSize - sourceOffset);
			if (copyCount)
			{
				segments.push_back({
					destinationRangeStarts[destinationRangeIndex].ptr +
						static_cast<SIZE_T>(destinationOffset) * descriptorIncrementSize,
					sourceRangeStarts[sourceRangeIndex].ptr +
						static_cast<SIZE_T>(sourceOffset) * descriptorIncrementSize,
					copyCount });
			}

			destinationOffset += copyCount;
			sourceOffset += copyCount;
			if (destinationOffset == destinationRangeSize)
			{
				++destinationRangeIndex;
				destinationOffset = 0;
			}
			if (sourceOffset == sourceRangeSize)
			{
				++sourceRangeIndex;
				sourceOffset = 0;
			}
		}
		if (segments.empty())
			return;

		std::vector<PendingDescriptorCopy> pendingCopies;
		bool hasTrackedDestination = false;
		{
			std::shared_lock<std::shared_mutex> lock(gDescriptorMutex);
			for (const DescriptorCopySegment& segment : segments)
			{
				QueueTrackedDescriptorCopiesLocked(segment, descriptorIncrementSize, pendingCopies);
				hasTrackedDestination = hasTrackedDestination ||
					HasTrackedDescriptorInDestinationRangeLocked(segment, descriptorIncrementSize);
			}
		}

		if (!pendingCopies.empty() || hasTrackedDestination)
		{
			std::unique_lock<std::shared_mutex> lock(gDescriptorMutex);
			for (const DescriptorCopySegment& segment : segments)
				EraseTrackedDescriptorRangeLocked(segment, descriptorIncrementSize);
			ApplyPendingDescriptorCopiesLocked(pendingCopies);
		}
	}

	void CopyDescriptorsSimple(
		UINT descriptorCount,
		D3D12_CPU_DESCRIPTOR_HANDLE destinationStart,
		D3D12_CPU_DESCRIPTOR_HANDLE sourceStart,
		UINT descriptorIncrementSize)
	{
		if (!descriptorIncrementSize)
			return;

		const DescriptorCopySegment segment{
			destinationStart.ptr,
			sourceStart.ptr,
			descriptorCount };
		std::vector<PendingDescriptorCopy> pendingCopies;
		bool hasTrackedDestination = false;
		{
			std::shared_lock<std::shared_mutex> lock(gDescriptorMutex);
			QueueTrackedDescriptorCopiesLocked(segment, descriptorIncrementSize, pendingCopies);
			hasTrackedDestination =
				HasTrackedDescriptorInDestinationRangeLocked(segment, descriptorIncrementSize);
		}

		if (!pendingCopies.empty() || hasTrackedDestination)
		{
			std::unique_lock<std::shared_mutex> lock(gDescriptorMutex);
			EraseTrackedDescriptorRangeLocked(segment, descriptorIncrementSize);
			ApplyPendingDescriptorCopiesLocked(pendingCopies);
		}
	}

	bool ResolveDescriptor(
		D3D12_CPU_DESCRIPTOR_HANDLE descriptor,
		RenderPass::ResourceBindingDiagnostic& outBinding)
	{
		std::shared_lock<std::shared_mutex> lock(gDescriptorMutex);
		const auto descriptorIt = gDescriptors.find(descriptor.ptr);
		if (descriptorIt == gDescriptors.end())
			return false;

		outBinding = descriptorIt->second;
		outBinding.cpuDescriptorHandle = descriptor.ptr;
		return true;
	}

	bool ResolveGpuVirtualAddress(
		D3D12_GPU_VIRTUAL_ADDRESS gpuAddress,
		RenderPass::ResourceBindingDiagnostic& outBinding)
	{
		std::shared_lock<std::shared_mutex> lock(gResourceMutex);
		return ResolveGpuVirtualAddressLocked(gpuAddress, outBinding);
	}

	void AnnotateRootDescriptor(
		ID3D12RootSignature* rootSignature,
		UINT rootParameterIndex,
		RenderPass::ResourceBindingDiagnostic& binding)
	{
		if (!rootSignature)
			return;

		std::shared_lock<std::shared_mutex> lock(gRootSignatureMutex);
		const auto rootSignatureIt = gRootSignatures.find(rootSignature);
		if (rootSignatureIt == gRootSignatures.end() ||
			rootParameterIndex >= rootSignatureIt->second.parameters.size())
		{
			return;
		}

		const RootParameterLayout& parameter = rootSignatureIt->second.parameters[rootParameterIndex];
		binding.shaderRegister = parameter.shaderRegister;
		binding.registerSpace = parameter.registerSpace;
	}

	void ResolveDescriptorTable(
		ID3D12RootSignature* rootSignature,
		UINT rootParameterIndex,
		D3D12_CPU_DESCRIPTOR_HANDLE tableStart,
		uint32_t descriptorHeapType,
		uint32_t firstDescriptorIndex,
		UINT descriptorIncrementSize,
		uint32_t maximumDescriptors,
		const std::string& pipeline,
		std::vector<RenderPass::ResourceBindingDiagnostic>& outBindings)
	{
		if (!rootSignature || !tableStart.ptr || !descriptorIncrementSize || !maximumDescriptors)
			return;

		std::shared_lock<std::shared_mutex> rootSignatureLock(gRootSignatureMutex);
		std::shared_lock<std::shared_mutex> descriptorLock(gDescriptorMutex);
		const auto rootSignatureIt = gRootSignatures.find(rootSignature);
		if (rootSignatureIt == gRootSignatures.end() ||
			rootParameterIndex >= rootSignatureIt->second.parameters.size())
		{
			return;
		}

		const RootParameterLayout& parameter = rootSignatureIt->second.parameters[rootParameterIndex];
		if (parameter.type != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
			return;

		uint32_t inspectedDescriptorCount = 0;
		for (const DescriptorRangeLayout& range : parameter.ranges)
		{
			const uint64_t availableDescriptorCount = range.descriptorCount == UINT_MAX
				? maximumDescriptors - inspectedDescriptorCount
				: range.descriptorCount;
			const uint32_t descriptorsToInspect = static_cast<uint32_t>((std::min<uint64_t>)(
				availableDescriptorCount,
				maximumDescriptors - inspectedDescriptorCount));
			for (uint32_t rangeDescriptorIndex = 0; rangeDescriptorIndex < descriptorsToInspect; ++rangeDescriptorIndex)
			{
				const uint32_t tableDescriptorIndex = range.tableOffset + rangeDescriptorIndex;
				const SIZE_T cpuHandle = tableStart.ptr +
					static_cast<SIZE_T>(tableDescriptorIndex) * descriptorIncrementSize;
				const auto descriptorIt = gDescriptors.find(cpuHandle);
				if (descriptorIt != gDescriptors.end())
				{
					RenderPass::ResourceBindingDiagnostic binding = descriptorIt->second;
					binding.pipeline = pipeline;
					binding.bindingType = DescriptorRangeTypeName(range.type);
					binding.rootParameterIndex = rootParameterIndex;
					binding.cpuDescriptorHandle = cpuHandle;
					binding.descriptorHeapType = descriptorHeapType;
					binding.descriptorIndex = firstDescriptorIndex + tableDescriptorIndex;
					binding.shaderRegister = range.baseShaderRegister + rangeDescriptorIndex;
					binding.registerSpace = range.registerSpace;
					outBindings.push_back(std::move(binding));
				}
			}

			inspectedDescriptorCount += descriptorsToInspect;
			if (inspectedDescriptorCount >= maximumDescriptors)
				break;
		}
	}

	bool GetDescriptorTableLayouts(
		ID3D12RootSignature* rootSignature,
		UINT maximumUnboundedDescriptors,
		std::vector<DescriptorTableLayout>& outLayouts)
	{
		outLayouts.clear();
		if (!rootSignature || !maximumUnboundedDescriptors)
			return false;

		std::shared_lock<std::shared_mutex> lock(gRootSignatureMutex);
		const auto rootSignatureIt = gRootSignatures.find(rootSignature);
		if (rootSignatureIt == gRootSignatures.end())
			return false;

		for (UINT parameterIndex = 0;
			parameterIndex < rootSignatureIt->second.parameters.size();
			++parameterIndex)
		{
			const RootParameterLayout& parameter = rootSignatureIt->second.parameters[parameterIndex];
			if (parameter.type != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE || parameter.ranges.empty())
				continue;

			DescriptorTableLayout table{};
			table.rootParameterIndex = parameterIndex;
			table.heapType = parameter.ranges.front().type == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER
				? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
				: D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			for (const DescriptorRangeLayout& range : parameter.ranges)
			{
				const UINT rangeCount = range.descriptorCount == UINT_MAX
					? maximumUnboundedDescriptors
					: range.descriptorCount;
				table.containsUnboundedRange = table.containsUnboundedRange || range.descriptorCount == UINT_MAX;
				if (range.tableOffset <= UINT_MAX - rangeCount)
					table.descriptorCount = (std::max)(table.descriptorCount, range.tableOffset + rangeCount);
			}
			if (table.descriptorCount)
				outLayouts.push_back(table);
		}
		return !outLayouts.empty();
	}

	bool FindDescriptorBinding(
		ID3D12RootSignature* rootSignature,
		D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
		UINT shaderRegister,
		UINT registerSpace,
		UINT maximumUnboundedDescriptors,
		DescriptorBindingLocation& outLocation)
	{
		outLocation = {};
		if (!rootSignature || !maximumUnboundedDescriptors)
			return false;

		std::shared_lock<std::shared_mutex> lock(gRootSignatureMutex);
		const auto rootSignatureIt = gRootSignatures.find(rootSignature);
		if (rootSignatureIt == gRootSignatures.end())
			return false;

		for (UINT parameterIndex = 0;
			parameterIndex < rootSignatureIt->second.parameters.size();
			++parameterIndex)
		{
			const RootParameterLayout& parameter = rootSignatureIt->second.parameters[parameterIndex];
			if (parameter.type != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
				continue;

			UINT tableDescriptorCount = 0;
			bool tableContainsUnboundedRange = false;
			for (const DescriptorRangeLayout& range : parameter.ranges)
			{
				const UINT rangeCount = range.descriptorCount == UINT_MAX
					? maximumUnboundedDescriptors
					: range.descriptorCount;
				tableContainsUnboundedRange = tableContainsUnboundedRange || range.descriptorCount == UINT_MAX;
				if (range.tableOffset <= UINT_MAX - rangeCount)
					tableDescriptorCount = (std::max)(tableDescriptorCount, range.tableOffset + rangeCount);
			}

			for (const DescriptorRangeLayout& range : parameter.ranges)
			{
				if (range.type != rangeType || range.registerSpace != registerSpace ||
					shaderRegister < range.baseShaderRegister)
				{
					continue;
				}

				const UINT registerOffset = shaderRegister - range.baseShaderRegister;
				const UINT availableCount = range.descriptorCount == UINT_MAX
					? maximumUnboundedDescriptors
					: range.descriptorCount;
				if (registerOffset >= availableCount)
					continue;

				outLocation.rootParameterIndex = parameterIndex;
				outLocation.tableOffset = range.tableOffset + registerOffset;
				outLocation.shaderRegister = shaderRegister;
				outLocation.registerSpace = registerSpace;
				outLocation.heapType = rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER
					? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
					: D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				outLocation.descriptorCount = tableDescriptorCount;
				outLocation.tableContainsUnboundedRange = tableContainsUnboundedRange;
				return true;
			}
		}
		return false;
	}

	bool FindUniqueDescriptorBindingByShaderRegister(
		ID3D12RootSignature* rootSignature,
		D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
		UINT shaderRegister,
		UINT maximumUnboundedDescriptors,
		DescriptorBindingLocation& outLocation)
	{
		outLocation = {};
		if (!rootSignature || !maximumUnboundedDescriptors)
			return false;

		std::shared_lock<std::shared_mutex> lock(gRootSignatureMutex);
		const auto rootSignatureIt = gRootSignatures.find(rootSignature);
		if (rootSignatureIt == gRootSignatures.end())
			return false;

		bool foundMatch = false;
		for (UINT parameterIndex = 0;
			parameterIndex < rootSignatureIt->second.parameters.size();
			++parameterIndex)
		{
			const RootParameterLayout& parameter = rootSignatureIt->second.parameters[parameterIndex];
			if (parameter.type != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
				continue;

			UINT tableDescriptorCount = 0;
			bool tableContainsUnboundedRange = false;
			for (const DescriptorRangeLayout& range : parameter.ranges)
			{
				const UINT rangeCount = range.descriptorCount == UINT_MAX
					? maximumUnboundedDescriptors
					: range.descriptorCount;
				tableContainsUnboundedRange = tableContainsUnboundedRange || range.descriptorCount == UINT_MAX;
				if (range.tableOffset <= UINT_MAX - rangeCount)
					tableDescriptorCount = (std::max)(tableDescriptorCount, range.tableOffset + rangeCount);
			}

			for (const DescriptorRangeLayout& range : parameter.ranges)
			{
				if (range.type != rangeType || shaderRegister < range.baseShaderRegister)
					continue;

				const UINT registerOffset = shaderRegister - range.baseShaderRegister;
				const UINT availableCount = range.descriptorCount == UINT_MAX
					? maximumUnboundedDescriptors
					: range.descriptorCount;
				if (registerOffset >= availableCount)
					continue;

				// A register repeated in multiple spaces is ambiguous. The caller must
				// require an explicit space instead of risking the wrong game resource.
				if (foundMatch)
				{
					outLocation = {};
					return false;
				}

				foundMatch = true;
				outLocation.rootParameterIndex = parameterIndex;
				outLocation.tableOffset = range.tableOffset + registerOffset;
				outLocation.shaderRegister = shaderRegister;
				outLocation.registerSpace = range.registerSpace;
				outLocation.heapType = rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER
					? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
					: D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				outLocation.descriptorCount = tableDescriptorCount;
				outLocation.tableContainsUnboundedRange = tableContainsUnboundedRange;
			}
		}

		return foundMatch;
	}
}
