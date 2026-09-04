#include "ShaderResource/ShaderResourceRuntime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <wrl/client.h>

#include "HookD3D12/HookD3D12RenderPass.h"
#include "IO/ShaderInjectorIO.h"
#include "Performance/PerformanceMetrics.h"
#include "RenderPass/RenderPassResourceRegistry.h"
#include "RenderPass/RenderPassTexturePool.h"
#include "ShaderResource/DatabaseShaderResources.h"
#include "ShaderResource/ShaderResourceDDS.h"
#include "StringHelper.h"

using Microsoft::WRL::ComPtr;

namespace ShaderResourceRuntime
{
	namespace
	{
		// Resource diagnostics intentionally inspect a small prefix, while an injected
		// shader can legally index much farther into an unbounded range. Expand cloned
		// tables through descriptors that the registry observed the game initialize.
		constexpr UINT MaximumAutoPreservedUnboundedDescriptors = 4096;
		constexpr UINT DefaultDescriptorPageCapacity = 8192;
		constexpr UINT DefaultSamplerPageCapacity = 2048;

		struct TextureGpu
		{
			ComPtr<ID3D12Resource> texture;
			ComPtr<ID3D12Resource> upload;
			ComPtr<ID3D12DescriptorHeap> srvHeap;
			D3D12_SHADER_RESOURCE_VIEW_DESC view{};
			bool uploadRecorded = false;
		};

		struct RootTableRestore
		{
			UINT rootParameterIndex = UINT32_MAX;
			D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle{};
			bool computePipeline = false;
		};

		struct ActiveTable
		{
			UINT rootParameterIndex = UINT32_MAX;
			D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
			UINT descriptorCount = 0;
			UINT customOffset = 0;
			D3D12_GPU_DESCRIPTOR_HANDLE originalGpu{};
			D3D12_CPU_DESCRIPTOR_HANDLE originalCpu{};
		};

		struct UnboundedDescriptorSpan
		{
			SIZE_T cpuStart = 0;
			UINT descriptorIncrementSize = 0;
			UINT scannedDescriptorCount = 0;
			UINT contiguousDescriptorCount = 0;
		};

		struct DescriptorHeapPage
		{
			ComPtr<ID3D12DescriptorHeap> heap;
			UINT capacity = 0;
			UINT usedDescriptors = 0;
		};

		struct DescriptorExecutionSlot
		{
			std::vector<DescriptorHeapPage> pages;
			std::vector<DescriptorHeapPage> samplerPages;
			ComPtr<ID3D12Fence> retirementFence;
			UINT64 retirementFenceValue = 0;
			bool recorded = false;
			bool submitted = false;
			bool retirementBlocked = false;
		};

		struct DescriptorAllocation
		{
			ID3D12DescriptorHeap* heap = nullptr;
			D3D12_CPU_DESCRIPTOR_HANDLE cpuStart{};
			D3D12_GPU_DESCRIPTOR_HANDLE gpuStart{};
		};

		struct CommandListSlot
		{
			ComPtr<ID3D12Device> device;
			UINT descriptorIncrementSize = 0;
			UINT samplerDescriptorIncrementSize = 0;
			ID3D12RootSignature* cachedRootSignature = nullptr;
			uint32_t cachedMaximumTrackedDescriptors = 0;
			std::vector<RenderPassResourceRegistry::DescriptorTableLayout> layouts;
			std::vector<ActiveTable> activeTables;
			std::unordered_map<uint64_t, std::vector<RenderPassResourceRegistry::DescriptorBindingLocation>> graphicsBindingLocations;
			std::unordered_map<uint64_t, std::vector<RenderPassResourceRegistry::DescriptorBindingLocation>> computeBindingLocations;
			std::unordered_map<std::string, TextureGpu*> resolvedTextures;
			std::vector<ID3D12DescriptorHeap*> restoreHeaps;
			std::vector<RootTableRestore> restoreRootTables;
			std::vector<std::unique_ptr<DescriptorExecutionSlot>> executionSlots;
			std::vector<DescriptorExecutionSlot*> recordedExecutionSlots;
			DescriptorExecutionSlot* currentExecutionSlot = nullptr;
			std::array<UnboundedDescriptorSpan, 16> unboundedDescriptorSpans{};
			size_t nextUnboundedDescriptorSpan = 0;
			bool pendingRestore = false;
		};

		struct QueueFence
		{
			ComPtr<ID3D12Fence> fence;
			UINT64 nextValue = 0;
		};

		std::mutex gTextureMutex;
		std::unordered_map<ID3D12Device*, std::unordered_map<std::string, std::unique_ptr<TextureGpu>>> gTextures;
		std::mutex gCommandListSlotMutex;
		std::unordered_map<ID3D12GraphicsCommandList*, std::unique_ptr<CommandListSlot>> gCommandListSlots;
		std::unordered_map<ID3D12CommandQueue*, QueueFence> gQueueFences;
		std::atomic<uint32_t> gRecordedCommandListCount = 0;
		thread_local ID3D12GraphicsCommandList* gCachedCommandList = nullptr;
		thread_local CommandListSlot* gCachedCommandListSlot = nullptr;

		CommandListSlot& GetCommandListSlot(ID3D12GraphicsCommandList* commandList)
		{
			if (gCachedCommandList == commandList && gCachedCommandListSlot)
				return *gCachedCommandListSlot;

			std::lock_guard<std::mutex> lock(gCommandListSlotMutex);
			auto& slot = gCommandListSlots[commandList];
			if (!slot)
				slot = std::make_unique<CommandListSlot>();
			gCachedCommandList = commandList;
			gCachedCommandListSlot = slot.get();
			return *slot;
		}

		CommandListSlot* FindCommandListSlot(ID3D12GraphicsCommandList* commandList)
		{
			if (gCachedCommandList == commandList)
				return gCachedCommandListSlot;

			std::lock_guard<std::mutex> lock(gCommandListSlotMutex);
			const auto slotIt = gCommandListSlots.find(commandList);
			if (slotIt == gCommandListSlots.end())
				return nullptr;
			gCachedCommandList = commandList;
			gCachedCommandListSlot = slotIt->second.get();
			return gCachedCommandListSlot;
		}

		bool IsExecutionSlotReusable(const DescriptorExecutionSlot& slot)
		{
			if (slot.recorded || slot.retirementBlocked)
				return false;
			return !slot.retirementFence ||
				slot.retirementFence->GetCompletedValue() >= slot.retirementFenceValue;
		}

		DescriptorExecutionSlot* AcquireExecutionSlotLocked(CommandListSlot& commandListSlot)
		{
			if (commandListSlot.currentExecutionSlot &&
				commandListSlot.currentExecutionSlot->recorded)
			{
				return commandListSlot.currentExecutionSlot;
			}

			DescriptorExecutionSlot* selectedSlot = nullptr;
			for (const std::unique_ptr<DescriptorExecutionSlot>& slot : commandListSlot.executionSlots)
			{
				if (IsExecutionSlotReusable(*slot))
				{
					selectedSlot = slot.get();
					break;
				}
			}
			if (!selectedSlot)
			{
				commandListSlot.executionSlots.push_back(std::make_unique<DescriptorExecutionSlot>());
				selectedSlot = commandListSlot.executionSlots.back().get();
			}

			for (DescriptorHeapPage& page : selectedSlot->pages)
				page.usedDescriptors = 0;
			for (DescriptorHeapPage& page : selectedSlot->samplerPages)
				page.usedDescriptors = 0;
			selectedSlot->recorded = true;
			selectedSlot->submitted = false;
			if (commandListSlot.recordedExecutionSlots.empty())
				gRecordedCommandListCount.fetch_add(1, std::memory_order_release);
			commandListSlot.recordedExecutionSlots.push_back(selectedSlot);
			commandListSlot.currentExecutionSlot = selectedSlot;
			return selectedSlot;
		}

		bool AcquireDescriptorAllocation(
			CommandListSlot& commandListSlot,
			ID3D12Device* device,
			D3D12_DESCRIPTOR_HEAP_TYPE heapType,
			UINT descriptorCount,
			DescriptorAllocation& outAllocation,
			std::string& outError)
		{
			outAllocation = {};
			if (!device || !descriptorCount ||
				(heapType != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
					heapType != D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER))
			{
				outError = "Shader-resource descriptor allocation is invalid.";
				return false;
			}
			if (heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER &&
				descriptorCount > DefaultSamplerPageCapacity)
			{
				outError = "Sampler descriptor allocation exceeds the shader-visible D3D12 sampler-heap limit.";
				return false;
			}

			std::lock_guard<std::mutex> lock(gCommandListSlotMutex);
			DescriptorExecutionSlot* executionSlot = AcquireExecutionSlotLocked(commandListSlot);
			std::vector<DescriptorHeapPage>& pages = heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
				? executionSlot->samplerPages
				: executionSlot->pages;
			DescriptorHeapPage* selectedPage = nullptr;
			for (DescriptorHeapPage& page : pages)
			{
				if (page.capacity - page.usedDescriptors >= descriptorCount)
				{
					selectedPage = &page;
					break;
				}
			}

			if (!selectedPage)
			{
				D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
				heapDescription.Type = heapType;
				heapDescription.NumDescriptors = (std::max)(
					descriptorCount,
					heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
						? DefaultSamplerPageCapacity
						: DefaultDescriptorPageCapacity);
				heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
				DescriptorHeapPage page{};
				const HRESULT result = device->CreateDescriptorHeap(
					&heapDescription,
					IID_PPV_ARGS(&page.heap));
				if (FAILED(result) || !page.heap)
				{
					outError = std::string(heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
						? "Sampler descriptor heap creation failed with "
						: "Shader-resource descriptor heap creation failed with ") +
						StringHelper::FormatHRESULT(result);
					return false;
				}
				page.capacity = heapDescription.NumDescriptors;
				pages.push_back(std::move(page));
				selectedPage = &pages.back();
			}

			const UINT rangeOffset = selectedPage->usedDescriptors;
			selectedPage->usedDescriptors += descriptorCount;
			const UINT increment = device->GetDescriptorHandleIncrementSize(heapType);
			outAllocation.heap = selectedPage->heap.Get();
			outAllocation.cpuStart = selectedPage->heap->GetCPUDescriptorHandleForHeapStart();
			outAllocation.cpuStart.ptr += static_cast<SIZE_T>(rangeOffset) * increment;
			outAllocation.gpuStart = selectedPage->heap->GetGPUDescriptorHandleForHeapStart();
			outAllocation.gpuStart.ptr += static_cast<UINT64>(rangeOffset) * increment;
			return true;
		}

		UINT CountPreservedUnboundedDescriptors(
			CommandListSlot& slot,
			D3D12_CPU_DESCRIPTOR_HANDLE tableStart,
			UINT descriptorIncrementSize,
			UINT availableDescriptorCount,
			UINT configuredDescriptorCount)
		{
			const UINT scanLimit = (std::min)(
				availableDescriptorCount,
				(std::max)(configuredDescriptorCount, MaximumAutoPreservedUnboundedDescriptors));
			for (const UnboundedDescriptorSpan& cachedSpan : slot.unboundedDescriptorSpans)
			{
				if (cachedSpan.cpuStart != tableStart.ptr ||
					cachedSpan.descriptorIncrementSize != descriptorIncrementSize)
				{
					continue;
				}

				const bool completeSpan =
					cachedSpan.contiguousDescriptorCount < cachedSpan.scannedDescriptorCount;
				if (completeSpan || cachedSpan.scannedDescriptorCount >= scanLimit)
				{
					return (std::min)(
						availableDescriptorCount,
						(std::max)(configuredDescriptorCount, cachedSpan.contiguousDescriptorCount));
				}
			}

			const UINT contiguousDescriptorCount =
				RenderPassResourceRegistry::CountContiguousDescriptors(
					tableStart,
					descriptorIncrementSize,
					scanLimit);
			if (contiguousDescriptorCount)
			{
				slot.unboundedDescriptorSpans[slot.nextUnboundedDescriptorSpan] = {
					tableStart.ptr,
					descriptorIncrementSize,
					scanLimit,
					contiguousDescriptorCount };
				slot.nextUnboundedDescriptorSpan =
					(slot.nextUnboundedDescriptorSpan + 1) % slot.unboundedDescriptorSpans.size();
			}

			return (std::min)(
				availableDescriptorCount,
				(std::max)(configuredDescriptorCount, contiguousDescriptorCount));
		}

		TextureGpu* GetOrCreateTexture(
			ID3D12Device* device,
			ID3D12GraphicsCommandList* commandList,
			const ShaderResource::TextureDisk& disk,
			std::string& outError)
		{
			if (!disk.validationError.empty())
			{
				outError = "DDS resource is invalid: " + disk.id + ". " + disk.validationError;
				return nullptr;
			}

			std::lock_guard<std::mutex> lock(gTextureMutex);
			auto& cachedEntry = gTextures[device][disk.id];
			if (!cachedEntry)
				cachedEntry = std::make_unique<TextureGpu>();
			TextureGpu& cached = *cachedEntry;
			if (cached.texture)
				return &cached;

			ShaderResourceDDS::Image image{};
			if (!ShaderResourceDDS::Load(disk.filePath, image, outError))
				return nullptr;
			const ShaderResourceDDS::Metadata& metadata = image.metadata;
			const bool texture3D = metadata.dimension == ShaderResource::TextureDimension::Texture3D;

			D3D12_FEATURE_DATA_FORMAT_INFO formatInformation{};
			formatInformation.Format = metadata.format;
			if (FAILED(device->CheckFeatureSupport(
				D3D12_FEATURE_FORMAT_INFO,
				&formatInformation,
				sizeof(formatInformation))) ||
				formatInformation.PlaneCount != 1)
			{
				outError = "DDS format is unsupported or uses multiple planes: " +
					std::to_string(static_cast<uint32_t>(metadata.format));
				return nullptr;
			}

			D3D12_RESOURCE_DESC description{};
			description.Dimension = texture3D
				? D3D12_RESOURCE_DIMENSION_TEXTURE3D
				: D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			description.Width = metadata.width;
			description.Height = metadata.height;
			description.DepthOrArraySize = static_cast<UINT16>(texture3D ? metadata.depth : metadata.arraySize);
			description.MipLevels = static_cast<UINT16>(metadata.mipLevels);
			description.Format = metadata.format;
			description.SampleDesc.Count = 1;
			description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

			D3D12_HEAP_PROPERTIES defaultHeap{ D3D12_HEAP_TYPE_DEFAULT };
			HRESULT result = device->CreateCommittedResource(
				&defaultHeap,
				D3D12_HEAP_FLAG_NONE,
				&description,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(&cached.texture));
			if (FAILED(result))
			{
				outError = "DDS texture creation failed with " + StringHelper::FormatHRESULT(result);
				return nullptr;
			}

			const uint64_t subresourceCount64 = static_cast<uint64_t>(metadata.mipLevels) *
				(texture3D ? 1ull : metadata.arraySize);
			if (!subresourceCount64 || subresourceCount64 > (std::numeric_limits<UINT>::max)())
			{
				outError = "DDS subresource count is invalid.";
				cached = {};
				return nullptr;
			}
			const UINT subresourceCount = static_cast<UINT>(subresourceCount64);
			std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(subresourceCount);
			std::vector<UINT> rowCounts(subresourceCount);
			std::vector<UINT64> rowSizes(subresourceCount);
			UINT64 uploadSize = 0;
			device->GetCopyableFootprints(
				&description,
				0,
				subresourceCount,
				0,
				layouts.data(),
				rowCounts.data(),
				rowSizes.data(),
				&uploadSize);
			if (!uploadSize || uploadSize == (std::numeric_limits<UINT64>::max)())
			{
				outError = "D3D12 could not calculate DDS upload footprints.";
				cached = {};
				return nullptr;
			}

			D3D12_RESOURCE_DESC uploadDescription{};
			uploadDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			uploadDescription.Width = uploadSize;
			uploadDescription.Height = 1;
			uploadDescription.DepthOrArraySize = 1;
			uploadDescription.MipLevels = 1;
			uploadDescription.SampleDesc.Count = 1;
			uploadDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			D3D12_HEAP_PROPERTIES uploadHeap{ D3D12_HEAP_TYPE_UPLOAD };
			result = device->CreateCommittedResource(
				&uploadHeap,
				D3D12_HEAP_FLAG_NONE,
				&uploadDescription,
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(&cached.upload));
			if (FAILED(result))
			{
				outError = "DDS upload buffer creation failed with " + StringHelper::FormatHRESULT(result);
				cached = {};
				return nullptr;
			}

			uint8_t* mapped = nullptr;
			result = cached.upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
			if (FAILED(result) || !mapped)
			{
				outError = "DDS upload buffer mapping failed with " + StringHelper::FormatHRESULT(result);
				cached = {};
				return nullptr;
			}

			size_t sourceOffset = 0;
			for (UINT subresource = 0; subresource < subresourceCount; ++subresource)
			{
				const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& layout = layouts[subresource];
				const UINT64 sourceRowBytes64 = rowSizes[subresource];
				const UINT sourceRows = rowCounts[subresource];
				const UINT sourceDepth = layout.Footprint.Depth;
				if (!sourceRowBytes64 || sourceRowBytes64 > layout.Footprint.RowPitch ||
					sourceRowBytes64 > (std::numeric_limits<size_t>::max)())
				{
					cached.upload->Unmap(0, nullptr);
					outError = "DDS row layout is unsupported.";
					cached = {};
					return nullptr;
				}

				const size_t sourceRowBytes = static_cast<size_t>(sourceRowBytes64);
				if (sourceRows > 0 && sourceRowBytes > (std::numeric_limits<size_t>::max)() / sourceRows)
				{
					cached.upload->Unmap(0, nullptr);
					outError = "DDS slice size overflows the host address space.";
					cached = {};
					return nullptr;
				}
				const size_t sourceSliceBytes = sourceRowBytes * sourceRows;
				if (sourceDepth > 0 && sourceSliceBytes > (std::numeric_limits<size_t>::max)() / sourceDepth)
				{
					cached.upload->Unmap(0, nullptr);
					outError = "DDS subresource size overflows the host address space.";
					cached = {};
					return nullptr;
				}
				const size_t sourceSubresourceBytes = sourceSliceBytes * sourceDepth;
				if (sourceOffset > image.pixels.size() ||
					sourceSubresourceBytes > image.pixels.size() - sourceOffset)
				{
					cached.upload->Unmap(0, nullptr);
					outError = "DDS subresource data is truncated at subresource " +
						std::to_string(subresource) + ".";
					cached = {};
					return nullptr;
				}

				const size_t destinationSlicePitch =
					static_cast<size_t>(layout.Footprint.RowPitch) * sourceRows;
				for (UINT depthSlice = 0; depthSlice < sourceDepth; ++depthSlice)
				{
					for (UINT row = 0; row < sourceRows; ++row)
					{
						const size_t destinationOffset = static_cast<size_t>(layout.Offset) +
							static_cast<size_t>(depthSlice) * destinationSlicePitch +
							static_cast<size_t>(row) * layout.Footprint.RowPitch;
						const size_t rowSourceOffset = sourceOffset +
							static_cast<size_t>(depthSlice) * sourceSliceBytes +
							static_cast<size_t>(row) * sourceRowBytes;
						std::memcpy(
							mapped + destinationOffset,
							image.pixels.data() + rowSourceOffset,
							sourceRowBytes);
					}
				}
				sourceOffset += sourceSubresourceBytes;
			}
			cached.upload->Unmap(0, nullptr);

			for (UINT subresource = 0; subresource < subresourceCount; ++subresource)
			{
				D3D12_TEXTURE_COPY_LOCATION destination{};
				destination.pResource = cached.texture.Get();
				destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
				destination.SubresourceIndex = subresource;
				D3D12_TEXTURE_COPY_LOCATION source{};
				source.pResource = cached.upload.Get();
				source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
				source.PlacedFootprint = layouts[subresource];
				commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
			}

			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = cached.texture.Get();
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			commandList->ResourceBarrier(1, &barrier);

			cached.view = {};
			cached.view.Format = metadata.format;
			cached.view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			switch (metadata.dimension)
			{
				case ShaderResource::TextureDimension::Texture2DArray:
					cached.view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
					cached.view.Texture2DArray.MipLevels = metadata.mipLevels;
					cached.view.Texture2DArray.ArraySize = metadata.arraySize;
					break;
				case ShaderResource::TextureDimension::TextureCube:
					cached.view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
					cached.view.TextureCube.MipLevels = metadata.mipLevels;
					break;
				case ShaderResource::TextureDimension::TextureCubeArray:
					cached.view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
					cached.view.TextureCubeArray.MipLevels = metadata.mipLevels;
					cached.view.TextureCubeArray.NumCubes = metadata.arraySize / 6u;
					break;
				case ShaderResource::TextureDimension::Texture3D:
					cached.view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
					cached.view.Texture3D.MipLevels = metadata.mipLevels;
					break;
				case ShaderResource::TextureDimension::Texture2D:
					cached.view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
					cached.view.Texture2D.MipLevels = metadata.mipLevels;
					break;
				case ShaderResource::TextureDimension::Unknown:
				default:
					outError = "DDS texture dimension is unsupported.";
					cached = {};
					return nullptr;
			}

			D3D12_DESCRIPTOR_HEAP_DESC srvHeapDescription{};
			srvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			srvHeapDescription.NumDescriptors = 1;
			result = device->CreateDescriptorHeap(&srvHeapDescription, IID_PPV_ARGS(&cached.srvHeap));
			if (FAILED(result))
			{
				outError = "DDS SRV descriptor heap creation failed with " + StringHelper::FormatHRESULT(result);
				cached = {};
				return nullptr;
			}
			device->CreateShaderResourceView(
				cached.texture.Get(),
				&cached.view,
				cached.srvHeap->GetCPUDescriptorHandleForHeapStart());
			cached.uploadRecorded = true;
			ShaderInjectorIO::WriteToLogFileSuccess(
				"ShaderResourceRuntime->GetOrCreateTexture: loaded resource=" + disk.id +
				" dimension=" + ShaderResource::TextureDimensionName(metadata.dimension) +
				" extent=" + std::to_string(metadata.width) + "x" +
				std::to_string(metadata.height) + "x" + std::to_string(metadata.depth) +
				" arraySize=" + std::to_string(metadata.arraySize) +
				" mipLevels=" + std::to_string(metadata.mipLevels) +
				" format=" + std::to_string(static_cast<uint32_t>(metadata.format)));
			return &cached;
		}

		const RenderPassMipChain::DescriptorHeapBinding* FindHeap(
			const RenderPassMipChain::GraphicsStateSnapshot& state,
			D3D12_GPU_DESCRIPTOR_HANDLE handle,
			D3D12_DESCRIPTOR_HEAP_TYPE type)
		{
			for (const auto& heap : state.descriptorHeaps)
			{
				if (heap.type != type || !heap.gpuStart.ptr || !heap.descriptorIncrementSize)
					continue;
				const UINT64 end = heap.gpuStart.ptr + static_cast<UINT64>(heap.descriptorCount) * heap.descriptorIncrementSize;
				if (handle.ptr >= heap.gpuStart.ptr && handle.ptr < end)
					return &heap;
			}
			return nullptr;
		}

		void RestoreRootTables(ID3D12GraphicsCommandList* commandList, const CommandListSlot& slot)
		{
			for (const RootTableRestore& binding : slot.restoreRootTables)
			{
				if (binding.computePipeline)
					commandList->SetComputeRootDescriptorTable(binding.rootParameterIndex, binding.descriptorHandle);
				else
					commandList->SetGraphicsRootDescriptorTable(binding.rootParameterIndex, binding.descriptorHandle);
			}
		}

		void ApplyInheritedRootBindings(
			const RenderPass::RenderPassDisk& renderPass,
			ID3D12GraphicsCommandList* commandList,
			const RenderPassMipChain::GraphicsStateSnapshot& gameState,
			bool computePipeline,
			bool includeDescriptorTables)
		{
			const bool inheritShaderResources =
				renderPass.inheritedGameBindings.shaderResources;
			const bool inheritConstantBuffers =
				renderPass.inheritedGameBindings.constantBuffers;
			for (const RenderPassMipChain::RootArgumentSnapshot& binding : gameState.rootBindings)
			{
				switch (binding.type)
				{
					case RenderPassMipChain::RootArgumentType::DescriptorTable:
						if (includeDescriptorTables &&
							(inheritShaderResources || inheritConstantBuffers) && binding.value)
						{
							if (computePipeline)
								commandList->SetComputeRootDescriptorTable(
									binding.rootParameterIndex, { binding.value });
							else
								commandList->SetGraphicsRootDescriptorTable(
									binding.rootParameterIndex, { binding.value });
						}
						break;
					case RenderPassMipChain::RootArgumentType::Constants:
						if (inheritConstantBuffers && !binding.constants.empty())
						{
							if (computePipeline)
								commandList->SetComputeRoot32BitConstants(
									binding.rootParameterIndex,
									static_cast<UINT>(binding.constants.size()),
									binding.constants.data(), 0);
							else
								commandList->SetGraphicsRoot32BitConstants(
									binding.rootParameterIndex,
									static_cast<UINT>(binding.constants.size()),
									binding.constants.data(), 0);
						}
						break;
					case RenderPassMipChain::RootArgumentType::ConstantBufferView:
						if (inheritConstantBuffers && binding.value)
						{
							if (computePipeline)
								commandList->SetComputeRootConstantBufferView(
									binding.rootParameterIndex, binding.value);
							else
								commandList->SetGraphicsRootConstantBufferView(
									binding.rootParameterIndex, binding.value);
						}
						break;
					case RenderPassMipChain::RootArgumentType::ShaderResourceView:
						if (inheritShaderResources && binding.value)
						{
							if (computePipeline)
								commandList->SetComputeRootShaderResourceView(
									binding.rootParameterIndex, binding.value);
							else
								commandList->SetGraphicsRootShaderResourceView(
									binding.rootParameterIndex, binding.value);
						}
						break;
					case RenderPassMipChain::RootArgumentType::UnorderedAccessView:
						if (inheritShaderResources && binding.value)
						{
							if (computePipeline)
								commandList->SetComputeRootUnorderedAccessView(
									binding.rootParameterIndex, binding.value);
							else
								commandList->SetGraphicsRootUnorderedAccessView(
									binding.rootParameterIndex, binding.value);
						}
						break;
				}
			}
		}
	}

	bool BindResources(
		const RenderPass::RenderPassDisk& renderPass,
		ID3D12GraphicsCommandList* commandList,
		const RenderPassMipChain::GraphicsStateSnapshot& gameState,
		const RenderPassMipChain::GraphicsStateSnapshot& oppositePipelineState,
		bool computePipeline,
		std::string& outError,
		const RuntimeTextureBindingOverride* runtimeTextureOverride)
	{
		outError.clear();
		const bool hasRuntimeShaderResource = std::any_of(
			renderPass.inputs.begin(),
			renderPass.inputs.end(),
			[](const RenderPass::LogicalResourceBindingDisk& input)
			{
				return input.origin == ShaderResource::ResourceOrigin::Runtime &&
					input.access == RenderPass::ResourceAccess::ShaderResource;
			});
		const bool hasRuntimeUnorderedAccess = std::any_of(
			renderPass.outputs.begin(),
			renderPass.outputs.end(),
			[](const RenderPass::LogicalResourceBindingDisk& output)
			{
				return output.origin == ShaderResource::ResourceOrigin::Runtime &&
					output.access == RenderPass::ResourceAccess::UnorderedAccess;
			});
		const bool hasDescriptorOverrides = !renderPass.shaderResources.empty() ||
			!renderPass.samplers.empty() ||
			hasRuntimeShaderResource || hasRuntimeUnorderedAccess;
		const bool hasInheritedBindings = renderPass.inheritedGameBindings.shaderResources ||
			renderPass.inheritedGameBindings.constantBuffers;
		if (!hasDescriptorOverrides && !hasInheritedBindings)
			return true;
		PerformanceMetrics::Increment(PerformanceMetrics::Counter::ShaderResourceBindAttempted);
		PerformanceMetrics::ScopedTimer bindTimer(PerformanceMetrics::Timing::BindShaderResources);
		if (!commandList || !gameState.rootSignature)
		{
			outError = "Shader resources require a captured root signature.";
			return false;
		}
		if (!hasDescriptorOverrides)
		{
			// The game descriptor heap is still active here. Reassert its tables and
			// root descriptors without allocating or cloning an injector heap.
			ApplyInheritedRootBindings(
				renderPass,
				commandList,
				gameState,
				computePipeline,
				true);
			PerformanceMetrics::Increment(PerformanceMetrics::Counter::ShaderResourceBindSucceeded);
			return true;
		}
		CommandListSlot& slot = GetCommandListSlot(commandList);
		if (!slot.device && FAILED(commandList->GetDevice(IID_PPV_ARGS(&slot.device))))
		{
			outError = "Could not query the D3D12 device.";
			return false;
		}
		ID3D12Device* device = slot.device.Get();

		if (slot.cachedRootSignature != gameState.rootSignature ||
			slot.cachedMaximumTrackedDescriptors != renderPass.maximumTrackedDescriptors)
		{
			slot.cachedRootSignature = gameState.rootSignature;
			slot.cachedMaximumTrackedDescriptors = renderPass.maximumTrackedDescriptors;
			slot.layouts.clear();
			slot.graphicsBindingLocations.clear();
			slot.computeBindingLocations.clear();
			if (!RenderPassResourceRegistry::GetDescriptorTableLayouts(
				gameState.rootSignature,
				renderPass.maximumTrackedDescriptors,
				slot.layouts))
			{
				outError = "Root-signature descriptor tables are unavailable.";
				return false;
			}
		}

		slot.activeTables.clear();
		const bool requiresResourceHeap = !renderPass.shaderResources.empty() ||
			hasRuntimeShaderResource || hasRuntimeUnorderedAccess;
		const bool requiresSamplerHeap = !renderPass.samplers.empty();
		UINT totalDescriptors = 0;
		UINT totalSamplerDescriptors = 0;
		for (const auto& binding : gameState.rootBindings)
		{
			if (binding.type != RenderPassMipChain::RootArgumentType::DescriptorTable)
				continue;
			const auto layoutIt = std::find_if(slot.layouts.begin(), slot.layouts.end(), [&](const auto& layout)
			{
				return layout.rootParameterIndex == binding.rootParameterIndex;
			});
			if (layoutIt == slot.layouts.end())
				continue;
			const auto* sourceHeap = FindHeap(gameState, { binding.value }, layoutIt->heapType);
			if (!sourceHeap)
				continue;
			const UINT64 byteOffset = binding.value - sourceHeap->gpuStart.ptr;
			const UINT64 heapBytes = static_cast<UINT64>(sourceHeap->descriptorCount) * sourceHeap->descriptorIncrementSize;
			if (byteOffset >= heapBytes)
				continue;

			UINT descriptorCount = layoutIt->descriptorCount;
			if (layoutIt->containsUnboundedRange &&
				(layoutIt->heapType == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
					layoutIt->heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER))
			{
				const UINT64 availableDescriptorCount64 =
					(heapBytes - byteOffset) / sourceHeap->descriptorIncrementSize;
				const UINT availableDescriptorCount = static_cast<UINT>((std::min)(
					availableDescriptorCount64,
					static_cast<UINT64>(UINT_MAX)));
				descriptorCount = CountPreservedUnboundedDescriptors(
					slot,
					{ sourceHeap->cpuStart.ptr + byteOffset },
					sourceHeap->descriptorIncrementSize,
					availableDescriptorCount,
					descriptorCount);
			}
			else
			{
				const UINT64 requiredBytes =
					static_cast<UINT64>(descriptorCount) * sourceHeap->descriptorIncrementSize;
				if (requiredBytes > heapBytes - byteOffset)
					continue;
			}
			if (!descriptorCount)
				continue;

			ActiveTable table{};
			table.rootParameterIndex = binding.rootParameterIndex;
			table.heapType = layoutIt->heapType;
			table.descriptorCount = descriptorCount;
			table.originalGpu = { binding.value };
			table.originalCpu = { sourceHeap->cpuStart.ptr + byteOffset };
			if (table.heapType == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
			{
				if (!requiresResourceHeap)
				{
					slot.activeTables.push_back(table);
					continue;
				}
				if (table.descriptorCount > UINT_MAX - totalDescriptors)
				{
					outError = "Shader-resource descriptor-table clone exceeds D3D12 limits.";
					return false;
				}
				table.customOffset = totalDescriptors;
				totalDescriptors += table.descriptorCount;
			}
			else if (table.heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
			{
				if (!requiresSamplerHeap)
				{
					slot.activeTables.push_back(table);
					continue;
				}
				if (table.descriptorCount > UINT_MAX - totalSamplerDescriptors)
				{
					outError = "Sampler descriptor-table clone exceeds D3D12 limits.";
					return false;
				}
				table.customOffset = totalSamplerDescriptors;
				totalSamplerDescriptors += table.descriptorCount;
			}
			slot.activeTables.push_back(table);
		}
		if (requiresResourceHeap && !totalDescriptors)
		{
			outError = "No CBV/SRV/UAV descriptor table is active.";
			return false;
		}
		if (requiresSamplerHeap && !totalSamplerDescriptors)
		{
			outError = "No dynamic sampler descriptor table is active. Static samplers cannot be overridden.";
			return false;
		}

		if (!slot.descriptorIncrementSize)
			slot.descriptorIncrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		if (!slot.samplerDescriptorIncrementSize)
			slot.samplerDescriptorIncrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
		DescriptorAllocation descriptorAllocation{};
		if (requiresResourceHeap && totalDescriptors && !AcquireDescriptorAllocation(
			slot,
			device,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			totalDescriptors,
			descriptorAllocation,
			outError))
		{
			return false;
		}
		DescriptorAllocation samplerAllocation{};
		if (requiresSamplerHeap && totalSamplerDescriptors && !AcquireDescriptorAllocation(
			slot,
			device,
			D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
			totalSamplerDescriptors,
			samplerAllocation,
			outError))
		{
			return false;
		}
		const UINT increment = slot.descriptorIncrementSize;
		const UINT samplerIncrement = slot.samplerDescriptorIncrementSize;
		const D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = descriptorAllocation.cpuStart;
		const D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = descriptorAllocation.gpuStart;
		const D3D12_CPU_DESCRIPTOR_HANDLE samplerCpuStart = samplerAllocation.cpuStart;
		const D3D12_GPU_DESCRIPTOR_HANDLE samplerGpuStart = samplerAllocation.gpuStart;
		for (const ActiveTable& table : slot.activeTables)
		{
			if (table.heapType == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV && descriptorAllocation.heap)
				device->CopyDescriptorsSimple(table.descriptorCount,
					{ cpuStart.ptr + static_cast<SIZE_T>(table.customOffset) * increment }, table.originalCpu,
					D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			else if (table.heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER && samplerAllocation.heap)
				device->CopyDescriptorsSimple(table.descriptorCount,
					{ samplerCpuStart.ptr + static_cast<SIZE_T>(table.customOffset) * samplerIncrement },
					table.originalCpu,
					D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
		}

		const auto resolveDescriptorDestination = [&](D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
			uint32_t shaderRegister,
			uint32_t registerSpace,
			D3D12_CPU_DESCRIPTOR_HANDLE& outDestination) -> bool
		{
			const bool samplerDescriptor = rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
			const D3D12_DESCRIPTOR_HEAP_TYPE requiredHeapType = samplerDescriptor
				? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
				: D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			const char registerPrefix = samplerDescriptor
				? 's'
				: (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV ? 'u' : 't');
			const char* bindingName = samplerDescriptor
				? "sampler"
				: (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV ? "UAV" : "SRV");
			const uint64_t bindingKey = (static_cast<uint64_t>(rangeType) << 56) |
				((static_cast<uint64_t>(registerSpace) & 0x0FFFFFFFull) << 28) |
				(static_cast<uint64_t>(shaderRegister) & 0x0FFFFFFFull);
			auto& bindingLocations = computePipeline
				? slot.computeBindingLocations
				: slot.graphicsBindingLocations;
			auto locationIt = bindingLocations.find(bindingKey);
			if (locationIt == bindingLocations.end())
			{
				std::vector<RenderPassResourceRegistry::DescriptorBindingLocation> locations;
				if (!RenderPassResourceRegistry::GetDescriptorBindingCandidates(
					gameState.rootSignature,
					rangeType,
					shaderRegister,
					registerSpace,
					renderPass.maximumTrackedDescriptors,
					computePipeline ? D3D12_SHADER_VISIBILITY_ALL : D3D12_SHADER_VISIBILITY_PIXEL,
					locations))
				{
					outError = "No " + std::string(bindingName) +
						" root binding exists for " + registerPrefix + std::to_string(shaderRegister) +
						", space" + std::to_string(registerSpace) + ".";
					return false;
				}
				locationIt = bindingLocations.emplace(bindingKey, std::move(locations)).first;
			}

			const RenderPassResourceRegistry::DescriptorBindingLocation* location = nullptr;
			const ActiveTable* activeTable = nullptr;
			for (const auto& candidate : locationIt->second)
			{
				const auto tableIt = std::find_if(slot.activeTables.begin(), slot.activeTables.end(), [&](const auto& table)
				{
					return table.rootParameterIndex == candidate.rootParameterIndex &&
						table.heapType == requiredHeapType &&
						candidate.tableOffset < table.descriptorCount;
				});
				if (tableIt != slot.activeTables.end())
				{
					location = &candidate;
					activeTable = &*tableIt;
					break;
				}
			}
			if (!location || !activeTable)
			{
				outError = "No compatible active " + std::string(bindingName) +
					" table exists for " + registerPrefix + std::to_string(shaderRegister) + ", space" +
					std::to_string(registerSpace) + " on this draw or dispatch.";
				return false;
			}
			const UINT descriptorOffset = activeTable->customOffset + location->tableOffset;
			const D3D12_CPU_DESCRIPTOR_HANDLE destinationStart = samplerDescriptor
				? samplerCpuStart
				: cpuStart;
			const UINT destinationIncrement = samplerDescriptor ? samplerIncrement : increment;
			outDestination = {
				destinationStart.ptr + static_cast<SIZE_T>(descriptorOffset) * destinationIncrement
			};
			return true;
		};

		const auto bindDescriptor = [&](D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
			uint32_t shaderRegister,
			uint32_t registerSpace,
			D3D12_CPU_DESCRIPTOR_HANDLE sourceDescriptor) -> bool
		{
			D3D12_CPU_DESCRIPTOR_HANDLE destination{};
			if (!resolveDescriptorDestination(
				rangeType,
				shaderRegister,
				registerSpace,
				destination))
			{
				return false;
			}

			const D3D12_DESCRIPTOR_HEAP_TYPE heapType = rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER
				? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
				: D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			device->CopyDescriptorsSimple(
				1,
				destination,
				sourceDescriptor,
				heapType);
			return true;
		};

		for (const RenderPass::ShaderResourceReferenceDisk& reference : renderPass.shaderResources)
		{
			const ShaderResource::TextureDisk* disk = DatabaseShaderResources::FindShaderResourceById(reference.resourceId);
			if (!disk)
			{
				outError = "Shader resource is missing: " + reference.resourceId;
				return false;
			}
			TextureGpu* texture = nullptr;
			const auto cachedTextureIt = slot.resolvedTextures.find(reference.resourceId);
			if (cachedTextureIt != slot.resolvedTextures.end())
				texture = cachedTextureIt->second;
			else
			{
				texture = GetOrCreateTexture(device, commandList, *disk, outError);
				if (texture)
					slot.resolvedTextures.emplace(reference.resourceId, texture);
			}
			if (!texture)
				return false;
			if (!bindDescriptor(
				D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				reference.shaderRegister,
				reference.registerSpace,
				texture->srvHeap->GetCPUDescriptorHandleForHeapStart()))
			{
				return false;
			}
		}

		for (const RenderPass::LogicalResourceBindingDisk& input : renderPass.inputs)
		{
			if (input.origin != ShaderResource::ResourceOrigin::Runtime ||
				input.access != RenderPass::ResourceAccess::ShaderResource)
			{
				continue;
			}

			D3D12_CPU_DESCRIPTOR_HANDLE sourceDescriptor{};
			if (runtimeTextureOverride && runtimeTextureOverride->resourceId == input.resourceId)
				sourceDescriptor = runtimeTextureOverride->shaderResourceView;
			else
			{
				RenderPassTexturePool::TextureView runtimeTexture;
				if (RenderPassTexturePool::GetInputTexture(input.resourceId, input.temporalView, runtimeTexture))
					sourceDescriptor = runtimeTexture.shaderResourceView;
			}

			if (!sourceDescriptor.ptr)
			{
				if (input.optional)
					continue;
				outError = "Runtime shader resource is unavailable: " + input.resourceId;
				return false;
			}
			if (!bindDescriptor(
				D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				input.shaderRegister,
				input.registerSpace,
				sourceDescriptor))
				return false;
		}

		for (const RenderPass::LogicalResourceBindingDisk& output : renderPass.outputs)
		{
			if (output.origin != ShaderResource::ResourceOrigin::Runtime ||
				output.access != RenderPass::ResourceAccess::UnorderedAccess)
			{
				continue;
			}

			D3D12_CPU_DESCRIPTOR_HANDLE sourceDescriptor{};
			if (runtimeTextureOverride &&
				runtimeTextureOverride->unorderedAccessResourceId == output.resourceId)
			{
				sourceDescriptor = runtimeTextureOverride->unorderedAccessView;
			}
			else
			{
				RenderPassTexturePool::TextureView runtimeTexture;
				if (RenderPassTexturePool::GetTexture(
					output.resourceId,
					output.temporalView,
					runtimeTexture))
				{
					sourceDescriptor = runtimeTexture.unorderedAccessView;
				}
			}
			if (!sourceDescriptor.ptr)
			{
				if (output.optional)
					continue;
				outError = "Runtime unordered-access output is unavailable: " + output.resourceId;
				return false;
			}

			if (!bindDescriptor(
				D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
				output.shaderRegister,
				output.registerSpace,
				sourceDescriptor))
			{
				return false;
			}
		}

		for (const RenderPass::SamplerStateDisk& sampler : renderPass.samplers)
		{
			D3D12_CPU_DESCRIPTOR_HANDLE destination{};
			if (!resolveDescriptorDestination(
				D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,
				sampler.shaderRegister,
				sampler.registerSpace,
				destination))
			{
				return false;
			}

			D3D12_SAMPLER_DESC description{};
			description.Filter = static_cast<D3D12_FILTER>(sampler.filter);
			description.AddressU = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(sampler.addressU);
			description.AddressV = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(sampler.addressV);
			description.AddressW = static_cast<D3D12_TEXTURE_ADDRESS_MODE>(sampler.addressW);
			description.MipLODBias = sampler.mipLodBias;
			description.MaxAnisotropy = (std::clamp)(sampler.maximumAnisotropy, 1u, 16u);
			description.ComparisonFunc =
				static_cast<D3D12_COMPARISON_FUNC>(sampler.comparisonFunction);
			std::copy(sampler.borderColor.begin(), sampler.borderColor.end(), description.BorderColor);
			description.MinLOD = sampler.minimumLod;
			description.MaxLOD = (std::max)(sampler.minimumLod, sampler.maximumLod);
			device->CreateSampler(&description, destination);
		}

		ID3D12DescriptorHeap* originalResourceHeap = nullptr;
		ID3D12DescriptorHeap* originalSamplerHeap = nullptr;
		for (const auto& heap : gameState.descriptorHeaps)
		{
			if (heap.type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
				originalResourceHeap = heap.heap;
			else if (heap.type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
				originalSamplerHeap = heap.heap;
		}
		ID3D12DescriptorHeap* resourceHeap = descriptorAllocation.heap
			? descriptorAllocation.heap
			: originalResourceHeap;
		ID3D12DescriptorHeap* samplerHeap = samplerAllocation.heap
			? samplerAllocation.heap
			: originalSamplerHeap;
		std::array<ID3D12DescriptorHeap*, 2> heaps{};
		UINT heapCount = 0;
		if (resourceHeap)
			heaps[heapCount++] = resourceHeap;
		if (samplerHeap)
			heaps[heapCount++] = samplerHeap;
		commandList->SetDescriptorHeaps(heapCount, heapCount ? heaps.data() : nullptr);
		for (const ActiveTable& table : slot.activeTables)
		{
			D3D12_GPU_DESCRIPTOR_HANDLE handle = table.originalGpu;
			if (table.heapType == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV && descriptorAllocation.heap)
			{
				handle = {
					gpuStart.ptr + static_cast<UINT64>(table.customOffset) * increment
				};
			}
			else if (table.heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER && samplerAllocation.heap)
			{
				handle = {
					samplerGpuStart.ptr + static_cast<UINT64>(table.customOffset) * samplerIncrement
				};
			}
			if (computePipeline)
				commandList->SetComputeRootDescriptorTable(table.rootParameterIndex, handle);
			else
				commandList->SetGraphicsRootDescriptorTable(table.rootParameterIndex, handle);
		}
		ApplyInheritedRootBindings(
			renderPass,
			commandList,
			gameState,
			computePipeline,
			false);
		slot.restoreHeaps.clear();
		for (const auto& heap : gameState.descriptorHeaps)
		{
			if (heap.heap)
				slot.restoreHeaps.push_back(heap.heap);
		}
		slot.restoreRootTables.clear();
		const auto appendRootTables = [&](const auto& rootBindings, bool restoreComputePipeline)
		{
			for (const auto& binding : rootBindings)
			{
				if (binding.type == RenderPassMipChain::RootArgumentType::DescriptorTable)
				{
					slot.restoreRootTables.push_back({
						binding.rootParameterIndex,
						{ binding.value },
						restoreComputePipeline });
				}
			}
		};
		appendRootTables(gameState.rootBindings, computePipeline);
		appendRootTables(oppositePipelineState.rootBindings, !computePipeline);
		slot.pendingRestore = true;
		PerformanceMetrics::Increment(PerformanceMetrics::Counter::ShaderResourceBindSucceeded);
		return true;
	}

	void RestoreResources(ID3D12GraphicsCommandList* commandList)
	{
		CommandListSlot* slot = FindCommandListSlot(commandList);
		if (!slot || !slot->pendingRestore)
			return;
		PerformanceMetrics::ScopedTimer restoreTimer(
			PerformanceMetrics::Timing::RestoreShaderResources);
		commandList->SetDescriptorHeaps(
			static_cast<UINT>(slot->restoreHeaps.size()),
			slot->restoreHeaps.empty() ? nullptr : slot->restoreHeaps.data());
		RestoreRootTables(commandList, *slot);
		slot->pendingRestore = false;
	}

	bool HasRecordedCommandListWork()
	{
		return gRecordedCommandListCount.load(std::memory_order_acquire) != 0;
	}

	void ResetCommandList(ID3D12GraphicsCommandList* commandList)
	{
		if (!commandList)
			return;

		std::lock_guard<std::mutex> lock(gCommandListSlotMutex);
		const auto slotIt = gCommandListSlots.find(commandList);
		if (slotIt == gCommandListSlots.end())
			return;

		CommandListSlot& commandListSlot = *slotIt->second;
		commandListSlot.pendingRestore = false;
		for (DescriptorExecutionSlot* executionSlot : commandListSlot.recordedExecutionSlots)
		{
			if (executionSlot)
				executionSlot->recorded = false;
		}
		if (!commandListSlot.recordedExecutionSlots.empty())
			gRecordedCommandListCount.fetch_sub(1, std::memory_order_acq_rel);
		commandListSlot.recordedExecutionSlots.clear();
		commandListSlot.currentExecutionSlot = nullptr;
	}

	void NotifyCommandListsSubmitted(
		ID3D12CommandQueue* commandQueue,
		UINT commandListCount,
		ID3D12CommandList* const* commandLists)
	{
		if (!HasRecordedCommandListWork() || !commandQueue || !commandLists || !commandListCount)
		{
			return;
		}
		const D3D12_COMMAND_LIST_TYPE queueType = commandQueue->GetDesc().Type;
		if (queueType != D3D12_COMMAND_LIST_TYPE_DIRECT &&
			queueType != D3D12_COMMAND_LIST_TYPE_COMPUTE)
			return;

		std::lock_guard<std::mutex> lock(gCommandListSlotMutex);
		thread_local std::vector<DescriptorExecutionSlot*> submittedSlots;
		thread_local std::unordered_set<DescriptorExecutionSlot*> uniqueSlots;
		submittedSlots.clear();
		uniqueSlots.clear();
		submittedSlots.reserve(commandListCount);
		uniqueSlots.reserve(commandListCount);
		for (UINT commandListIndex = 0; commandListIndex < commandListCount; ++commandListIndex)
		{
			ID3D12GraphicsCommandList* graphicsCommandList =
				reinterpret_cast<ID3D12GraphicsCommandList*>(commandLists[commandListIndex]);
			const auto slotIt = gCommandListSlots.find(graphicsCommandList);
			if (slotIt == gCommandListSlots.end())
				continue;
			for (DescriptorExecutionSlot* executionSlot : slotIt->second->recordedExecutionSlots)
			{
				if (executionSlot && uniqueSlots.insert(executionSlot).second)
					submittedSlots.push_back(executionSlot);
			}
		}
		if (submittedSlots.empty())
			return;

		QueueFence& queueFence = gQueueFences[commandQueue];
		if (!queueFence.fence)
		{
			ComPtr<ID3D12Device> device;
			if (FAILED(commandQueue->GetDevice(IID_PPV_ARGS(&device))) || !device ||
				FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&queueFence.fence))))
			{
				for (DescriptorExecutionSlot* executionSlot : submittedSlots)
				{
					executionSlot->submitted = true;
					executionSlot->retirementBlocked = true;
				}
				ShaderInjectorIO::WriteToLogFileError(
					"ShaderResourceRuntime->NotifyCommandListsSubmitted: could not create the retirement fence");
				return;
			}
		}

		const UINT64 fenceValue = ++queueFence.nextValue;
		const HRESULT result = commandQueue->Signal(queueFence.fence.Get(), fenceValue);
		if (FAILED(result))
		{
			for (DescriptorExecutionSlot* executionSlot : submittedSlots)
			{
				executionSlot->submitted = true;
				executionSlot->retirementBlocked = true;
			}
			ShaderInjectorIO::WriteToLogFileError(
				"ShaderResourceRuntime->NotifyCommandListsSubmitted: queue signal failed with " +
				StringHelper::FormatHRESULT(result));
			return;
		}

		for (DescriptorExecutionSlot* executionSlot : submittedSlots)
		{
			executionSlot->retirementFence = queueFence.fence;
			executionSlot->retirementFenceValue = fenceValue;
			executionSlot->submitted = true;
			executionSlot->retirementBlocked = false;
		}
	}

	void ReleaseResources()
	{
		{
			std::lock_guard<std::mutex> textureLock(gTextureMutex);
			gTextures.clear();
		}
		{
			std::lock_guard<std::mutex> slotLock(gCommandListSlotMutex);
			gCommandListSlots.clear();
			gQueueFences.clear();
			gRecordedCommandListCount.store(0, std::memory_order_release);
		}
		gCachedCommandList = nullptr;
		gCachedCommandListSlot = nullptr;
	}
}
