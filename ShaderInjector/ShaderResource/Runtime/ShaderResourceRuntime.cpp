#include "ShaderResource/Runtime/ShaderResourceRuntime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <wrl/client.h>

#include "HookD3D12/HookD3D12.h"
#include "IO/ShaderInjectorIO.h"
#include "Performance/PerformanceMetrics.h"
#include "RenderPass/RenderPassResourceRegistry.h"
#include "RenderPass/RenderPassTexturePool.h"
#include "ShaderResource/DatabaseShaderResources.h"
#include "ShaderResource/Runtime/ActiveTable.h"
#include "ShaderResource/Runtime/CommandListSlot.h"
#include "ShaderResource/Runtime/DescriptorAllocation.h"
#include "ShaderResource/Runtime/DescriptorExecutionSlot.h"
#include "ShaderResource/Runtime/DescriptorHeapPage.h"
#include "ShaderResource/Runtime/DescriptorOverride.h"
#include "ShaderResource/Runtime/QueueFence.h"
#include "ShaderResource/Runtime/RootTableRestore.h"
#include "ShaderResource/Runtime/ScopedResourceBindingBatch.h"
#include "ShaderResource/Runtime/SamplerOverride.h"
#include "ShaderResource/Runtime/TextureGPU.h"
#include "DDS/DDS.h"
#include "StringHelper.h"

using Microsoft::WRL::ComPtr;

namespace ShaderResourceRuntime
{
	namespace
	{
		constexpr UINT defaultDescriptorPageCapacity = 8192;
		constexpr UINT defaultSamplerPageCapacity = 2048;
		std::atomic<bool> gLoggedPartialResourceTable = false;
		std::atomic<bool> gLoggedPartialSamplerTable = false;
		std::atomic<bool> gLoggedIndependentSamplerTable = false;

		std::mutex gTextureMutex;
		std::unordered_map<ID3D12Device*, std::unordered_map<std::string, std::unique_ptr<TextureGPU>>> gTextures;
		//replaced textures stay alive: recorded command lists and descriptor copies may still reference them.
		std::vector<std::unique_ptr<TextureGPU>> gRetiredTextures;
		std::atomic<uint64_t> gTextureGeneration = 0;
		std::mutex gCommandListSlotMutex;
		std::mutex gPerformanceStatisticsMutex;
		PerformanceStatistics gLastPerformanceStatistics;
		std::unordered_map<ID3D12GraphicsCommandList*, std::unique_ptr<CommandListSlot>> gCommandListSlots;
		std::unordered_map<ID3D12CommandQueue*, QueueFence> gQueueFences;
		std::atomic<uint32_t> gRecordedCommandListCount = 0;
		thread_local ID3D12GraphicsCommandList* gCachedCommandList = nullptr;
		thread_local CommandListSlot* gCachedCommandListSlot = nullptr;
		thread_local ID3D12GraphicsCommandList* gResourceBindingBatchCommandList = nullptr;

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
			std::string& outError,
			ID3D12DescriptorHeap* preferredHeap = nullptr,
			UINT fallbackDescriptorCount = 0)
		{
			outAllocation = {};

			if (!device || !descriptorCount ||
				(heapType != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
				 heapType != D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER))
			{
				outError = "Shader-resource descriptor allocation is invalid.";
				return false;
			}

			if (!fallbackDescriptorCount)
				fallbackDescriptorCount = descriptorCount;

			if (heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER && fallbackDescriptorCount > defaultSamplerPageCapacity)
			{
				outError = "Sampler descriptor allocation exceeds the shader-visible D3D12 sampler-heap limit.";
				return false;
			}

			std::lock_guard<std::mutex> lock(gCommandListSlotMutex);
			DescriptorExecutionSlot* executionSlot = AcquireExecutionSlotLocked(commandListSlot);
			std::vector<DescriptorHeapPage>* pages = &executionSlot->pages;
			if (heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
				pages = &executionSlot->samplerPages;
			DescriptorHeapPage* selectedPage = nullptr;
			UINT allocationCount = descriptorCount;

			//append to the chain's current heap so previously copied tables remain bindable together.
			if (preferredHeap)
			{
				for (DescriptorHeapPage& page : *pages)
				{
					if (page.heap.Get() == preferredHeap && page.capacity - page.usedDescriptors >= descriptorCount)
					{
						selectedPage = &page;
						break;
					}
				}
			}

			if (!selectedPage)
			{
				//a heap switch needs room for all active tables, including formerly shared ones.
				allocationCount = fallbackDescriptorCount;

				for (DescriptorHeapPage& page : *pages)
				{
					if (page.capacity - page.usedDescriptors >= allocationCount)
					{
						selectedPage = &page;
						break;
					}
				}
			}

			if (!selectedPage)
			{
				D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
				heapDescription.Type = heapType;
				UINT pageCapacity = defaultDescriptorPageCapacity;
				if (heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
					pageCapacity = defaultSamplerPageCapacity;
				heapDescription.NumDescriptors = (std::max)(allocationCount, pageCapacity);
				heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
				DescriptorHeapPage page{};
				const HRESULT result = device->CreateDescriptorHeap(
					&heapDescription,
					IID_PPV_ARGS(&page.heap));

				if (FAILED(result) || !page.heap)
				{
					std::string heapError = "Shader-resource descriptor heap creation failed with ";
					if (heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
						heapError = "Sampler descriptor heap creation failed with ";
					outError = heapError + StringHelper::FormatHRESULT(result);
					return false;
				}

				page.capacity = heapDescription.NumDescriptors;

				//heap base handles never change; query them once rather than for every pass allocation.
				page.cpuStart = page.heap->GetCPUDescriptorHandleForHeapStart();
				page.gpuStart = page.heap->GetGPUDescriptorHandleForHeapStart();

				pages->push_back(std::move(page));
				selectedPage = &pages->back();
			}

			const UINT rangeOffset = selectedPage->usedDescriptors;
			selectedPage->usedDescriptors += allocationCount;
			UINT increment = commandListSlot.descriptorIncrementSize;

			if (heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
				increment = commandListSlot.samplerDescriptorIncrementSize;

			outAllocation.heap = selectedPage->heap.Get();
			outAllocation.cpuStart = selectedPage->cpuStart;
			outAllocation.cpuStart.ptr += static_cast<SIZE_T>(rangeOffset) * increment;
			outAllocation.gpuStart = selectedPage->gpuStart;
			outAllocation.gpuStart.ptr += static_cast<UINT64>(rangeOffset) * increment;
			outAllocation.descriptorCount = allocationCount;
			return true;
		}

		const SamplerOverride* FindSingleSamplerOverride(const std::vector<SamplerOverride>& samplerOverrides, size_t tableIndex)
		{
			const SamplerOverride* selectedSampler = nullptr;

			for (const SamplerOverride& candidate : samplerOverrides)
			{
				if (candidate.tableIndex != tableIndex)
					continue;

				//multi-sampler tables still use fresh copies; the common one-sampler case needs no extra key allocation.
				if (selectedSampler)
					return nullptr;

				selectedSampler = &candidate;
			}

			return selectedSampler;
		}

		bool PrepareChainTables(CommandListSlot& slot, ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, ChainDescriptorPool& pool, UINT totalDescriptors, const std::vector<SamplerOverride>& samplerOverrides, std::string& outError)
		{
			UINT missingDescriptors = 0;

			for (ActiveTable& table : slot.activeTables)
			{
				if (table.heapType != heapType)
					continue;

				table.needsCopy = true;
				const SamplerOverride* samplerOverride = nullptr;

				if (heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
					samplerOverride = FindSingleSamplerOverride(samplerOverrides, static_cast<size_t>(&table - slot.activeTables.data()));

				const bool reusableTable = table.sourceDescriptorCount == table.descriptorCount && (!table.hasOverrides || samplerOverride);

				if (reusableTable)
					table.sourceVersionKnown = RenderPassResourceRegistry::GetDescriptorContentVersion(table.originalCpu, table.descriptorCount, table.sourceVersion);

				if (reusableTable && table.sourceVersionKnown)
				{
					const auto cachedTable = std::find_if(pool.tables.begin(), pool.tables.end(), [&](const ChainDescriptorTable& candidate)
					{
						if (candidate.source.ptr != table.originalCpu.ptr || candidate.descriptorCount != table.descriptorCount || candidate.sourceVersion.heapGeneration != table.sourceVersion.heapGeneration || candidate.sourceVersion.revision != table.sourceVersion.revision || candidate.hasSamplerOverride != (samplerOverride != nullptr))
							return false;

						if (samplerOverride)
							return candidate.samplerOffset == samplerOverride->tableOffset && std::memcmp(&candidate.samplerDescription, &samplerOverride->description, sizeof(D3D12_SAMPLER_DESC)) == 0;

						return true;
					});

					if (cachedTable != pool.tables.end())
					{
						table.allocation = cachedTable->allocation;
						table.needsCopy = false;
					}
				}

				if (table.needsCopy)
					missingDescriptors += table.descriptorCount;
			}

			if (missingDescriptors)
			{
				DescriptorAllocation allocation;

				if (!AcquireDescriptorAllocation(slot, device, heapType, missingDescriptors, allocation, outError, pool.heap, totalDescriptors))
					return false;

				if (allocation.heap != pool.heap)
				{
					pool.Reset();
					pool.heap = allocation.heap;

					//never combine GPU handles from different heaps after a page rollover.
					for (ActiveTable& table : slot.activeTables)
						if (table.heapType == heapType)
							table.needsCopy = true;
				}

				UINT descriptorIncrement = slot.descriptorIncrementSize;

				if (heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
					descriptorIncrement = slot.samplerDescriptorIncrementSize;

				thread_local std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> copySources;
				thread_local std::vector<UINT> copySourceSizes;
				copySources.clear();
				copySourceSizes.clear();
				UINT descriptorOffset = 0;
				bool hasPartialSource = false;

				for (ActiveTable& table : slot.activeTables)
				{
					if (table.heapType != heapType || !table.needsCopy)
						continue;

					table.allocation = allocation;
					table.allocation.cpuStart.ptr += static_cast<SIZE_T>(descriptorOffset) * descriptorIncrement;
					table.allocation.gpuStart.ptr += static_cast<UINT64>(descriptorOffset) * descriptorIncrement;
					table.allocation.descriptorCount = table.descriptorCount;
					descriptorOffset += table.descriptorCount;
					if (table.originalCpu.ptr)
					{
						copySources.push_back(table.originalCpu);
						copySourceSizes.push_back(table.sourceDescriptorCount);
						hasPartialSource = hasPartialSource || table.sourceDescriptorCount < table.descriptorCount;
					}
					else if (heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
					{
						//the game may use only static samplers, leaving its dynamic table completely unbound.
						//initialize every slot before applying the custom pass's explicitly configured samplers.
						D3D12_SAMPLER_DESC description{};
						description.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
						description.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
						description.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
						description.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
						description.MaxAnisotropy = 1;
						description.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
						description.MaxLOD = D3D12_FLOAT32_MAX;

						for (UINT descriptorIndex = 0; descriptorIndex < table.descriptorCount; ++descriptorIndex)
						{
							D3D12_CPU_DESCRIPTOR_HANDLE destination = table.allocation.cpuStart;
							destination.ptr += static_cast<SIZE_T>(descriptorIndex) * descriptorIncrement;
							device->CreateSampler(&description, destination);
						}
					}
				}

				{
					PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Timing::CloneShaderResourceTables);

				//new tables are appended after inherited tables; UAVs are fully overridden and samplers receive defaults.
					UINT copiedDescriptorCount = 0;

					for (UINT sourceSize : copySourceSizes)
						copiedDescriptorCount += sourceSize;

					bool tablesCopied = true;

					if (hasPartialSource)
					{
						//a game shader can use a short table at the heap tail even when its root declares a larger range.
						//keep the destination offsets intact, copy only real source descriptors, and initialize the unused tail.
						for (const ActiveTable& table : slot.activeTables)
						{
							if (table.heapType != heapType || !table.needsCopy || !table.originalCpu.ptr)
								continue;

							if (!RenderPassResourceRegistry::CopyDescriptorTables(device, table.allocation.cpuStart, table.sourceDescriptorCount, 1, &table.originalCpu, &table.sourceDescriptorCount, heapType))
							{
								tablesCopied = false;
								break;
							}

							for (UINT descriptorIndex = table.sourceDescriptorCount; descriptorIndex < table.descriptorCount; ++descriptorIndex)
							{
								D3D12_CPU_DESCRIPTOR_HANDLE destination = table.allocation.cpuStart;
								destination.ptr += static_cast<SIZE_T>(descriptorIndex) * descriptorIncrement;

								if (heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
								{
									D3D12_SAMPLER_DESC description{};
									description.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
									description.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
									description.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
									description.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
									description.MaxAnisotropy = 1;
									description.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
									description.MaxLOD = D3D12_FLOAT32_MAX;
									device->CreateSampler(&description, destination);
								}
								else
								{
									D3D12_SHADER_RESOURCE_VIEW_DESC description{};
									description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
									description.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
									description.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
									description.Texture2D.MipLevels = 1;
									device->CreateShaderResourceView(nullptr, &description, destination);
								}
							}
						}
					}
					else if (!copySources.empty())
					{
						tablesCopied = RenderPassResourceRegistry::CopyDescriptorTables(device, allocation.cpuStart, copiedDescriptorCount, static_cast<UINT>(copySources.size()), copySources.data(), copySourceSizes.data(), heapType);
					}

					if (!tablesCopied)
					{
						outError = "Could not copy descriptor tables because an active source mirror is unavailable.";
						PerformanceMetrics::Increment(PerformanceMetrics::Counter::ShaderResourceTableCopySourceMissing);
						return false;
					}
				}

				PerformanceMetrics::Increment(PerformanceMetrics::Counter::ShaderResourceTableDescriptorsCopied, descriptorOffset);
				PerformanceMetrics::Increment(PerformanceMetrics::Counter::ShaderResourceCopyBatches);
			}

			for (const ActiveTable& table : slot.activeTables)
			{
				if (table.heapType != heapType)
					continue;

				if (!table.needsCopy)
				{
					PerformanceMetrics::Increment(PerformanceMetrics::Counter::ShaderResourceTablesReused);
					PerformanceMetrics::Increment(PerformanceMetrics::Counter::ShaderResourceTableDescriptorsReused, table.descriptorCount);
					continue;
				}

				if (!table.sourceVersionKnown)
					continue;

				ChainDescriptorTable cachedTable{table.originalCpu, table.descriptorCount, table.sourceVersion, table.allocation};

				if (table.hasOverrides)
				{
					const SamplerOverride* samplerOverride = FindSingleSamplerOverride(samplerOverrides, static_cast<size_t>(&table - slot.activeTables.data()));

					if (!samplerOverride)
						continue;

					cachedTable.hasSamplerOverride = true;
					cachedTable.samplerOffset = samplerOverride->tableOffset;
					cachedTable.samplerDescription = samplerOverride->description;
				}

				pool.tables.push_back(cachedTable);
			}

			return true;
		}

		TextureGPU* GetOrCreateTexture(
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
				cachedEntry = std::make_unique<TextureGPU>();

			TextureGPU& cached = *cachedEntry;

			if (cached.texture)
				return &cached;

			DDS::Image image{};

			if (!DDS::Load(disk.filePath, image, outError))
				return nullptr;

			const DDS::Metadata& metadata = image.metadata;
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
			description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			if (texture3D)
				description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
			description.Width = metadata.width;
			description.Height = metadata.height;
			description.DepthOrArraySize = static_cast<UINT16>(metadata.arraySize);
			if (texture3D)
				description.DepthOrArraySize = static_cast<UINT16>(metadata.depth);
			description.MipLevels = static_cast<UINT16>(metadata.mipLevels);
			description.Format = metadata.format;
			description.SampleDesc.Count = 1;
			description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

			D3D12_HEAP_PROPERTIES defaultHeap{D3D12_HEAP_TYPE_DEFAULT};
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

			uint64_t layerCount = metadata.arraySize;
			if (texture3D)
				layerCount = 1;
			const uint64_t subresourceCount64 = static_cast<uint64_t>(metadata.mipLevels) * layerCount;

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
			D3D12_HEAP_PROPERTIES uploadHeap{D3D12_HEAP_TYPE_UPLOAD};
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

				const size_t destinationSlicePitch = static_cast<size_t>(layout.Footprint.RowPitch) * sourceRows;

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
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
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

			D3D12_DESCRIPTOR_HEAP_DESC shaderResourceViewHeapDescription{};
			shaderResourceViewHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			shaderResourceViewHeapDescription.NumDescriptors = 1;
			result = device->CreateDescriptorHeap(&shaderResourceViewHeapDescription, IID_PPV_ARGS(&cached.shaderResourceViewHeap));

			if (FAILED(result))
			{
				outError = "DDS SRV descriptor heap creation failed with " + StringHelper::FormatHRESULT(result);
				cached = {};
				return nullptr;
			}

			device->CreateShaderResourceView(
				cached.texture.Get(),
				&cached.view,
				cached.shaderResourceViewHeap->GetCPUDescriptorHandleForHeapStart());
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
			const bool inheritShaderResources = renderPass.inheritedGameBindings.shaderResources;
			const bool inheritConstantBuffers = renderPass.inheritedGameBindings.constantBuffers;

			for (const RenderPassMipChain::RootArgumentSnapshot& binding : gameState.rootBindings)
			{
				switch (binding.type)
				{
				case RenderPassMipChain::RootArgumentType::DescriptorTable:
					if (includeDescriptorTables &&
						(inheritShaderResources || inheritConstantBuffers) && binding.value)
					{
						if (computePipeline)
							commandList->SetComputeRootDescriptorTable(binding.rootParameterIndex, {binding.value});
						else
							commandList->SetGraphicsRootDescriptorTable(binding.rootParameterIndex, {binding.value});
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
							commandList->SetComputeRootConstantBufferView(binding.rootParameterIndex, binding.value);
						else
							commandList->SetGraphicsRootConstantBufferView(binding.rootParameterIndex, binding.value);
					}
					break;
				case RenderPassMipChain::RootArgumentType::ShaderResourceView:
					if (inheritShaderResources && binding.value)
					{
						if (computePipeline)
							commandList->SetComputeRootShaderResourceView(binding.rootParameterIndex, binding.value);
						else
							commandList->SetGraphicsRootShaderResourceView(binding.rootParameterIndex, binding.value);
					}
					break;
				case RenderPassMipChain::RootArgumentType::UnorderedAccessView:
					if (inheritShaderResources && binding.value)
					{
						if (computePipeline)
							commandList->SetComputeRootUnorderedAccessView(binding.rootParameterIndex, binding.value);
						else
							commandList->SetGraphicsRootUnorderedAccessView(binding.rootParameterIndex, binding.value);
					}
					break;
				}
			}
		}
	} //namespace

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

		const bool hasShaderInput = std::any_of(
			renderPass.inputs.begin(),
			renderPass.inputs.end(),
			[](const RenderPass::LogicalResourceBindingDisk& input)
			{
				return (input.origin == ShaderResource::ResourceOrigin::Runtime ||
						input.origin == ShaderResource::ResourceOrigin::Disk) &&
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

		const bool hasDescriptorOverrides = !renderPass.samplers.empty() ||
											hasShaderInput || hasRuntimeUnorderedAccess;

		const bool hasInheritedBindings = renderPass.inheritedGameBindings.shaderResources ||
										  renderPass.inheritedGameBindings.constantBuffers;

		if (!hasDescriptorOverrides && !hasInheritedBindings)
		{
			RestoreResourcesImmediately(commandList);

			return true;
		}

		PerformanceMetrics::Increment(PerformanceMetrics::Counter::ShaderResourceBindAttempted);
		PerformanceMetrics::ScopedTimer bindTimer(PerformanceMetrics::Timing::BindShaderResources);

		if (!commandList || !gameState.rootSignature)
		{
			outError = "Shader resources require a captured root signature.";
			return false;
		}

		if (!hasDescriptorOverrides)
		{
			//these table handles belong to the game heap, so flush any earlier injector bindings first.
			RestoreResourcesImmediately(commandList);

			//The game descriptor heap is still active here. Reassert its tables and
			//root descriptors without allocating or cloning an injector heap.
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
		const bool bindingBatchActive = gResourceBindingBatchCommandList == commandList;

		//a texture reload replaced or removed textures this slot may still point at.
		const uint64_t textureGeneration = gTextureGeneration.load(std::memory_order_acquire);
		if (slot.resolvedTextureGeneration != textureGeneration)
		{
			slot.resolvedTextures.clear();
			slot.resolvedTextureGeneration = textureGeneration;
		}

		if (!bindingBatchActive)
		{
			slot.resourceTablePool.Reset();
			slot.samplerTablePool.Reset();
			slot.boundRootTables.clear();
		}

		if (!slot.device && FAILED(commandList->GetDevice(IID_PPV_ARGS(&slot.device))))
		{
			outError = "Could not query the D3D12 device.";
			return false;
		}

		ID3D12Device* device = slot.device.Get();

		//Resolve inputs before allocating/copying shader-visible tables. A missing
		//dependency must not consume a new descriptor range on every failed draw.
		thread_local std::vector<RenderPassTexturePool::TextureView> runtimeInputs;
		runtimeInputs.clear();
		runtimeInputs.resize(renderPass.inputs.size());

		for (size_t inputIndex = 0; inputIndex < renderPass.inputs.size(); ++inputIndex)
		{
			const auto& input = renderPass.inputs[inputIndex];

			if (input.origin != ShaderResource::ResourceOrigin::Runtime ||
				input.access != RenderPass::ResourceAccess::ShaderResource)
				continue;

			auto& texture = runtimeInputs[inputIndex];

			if (runtimeTextureOverride && runtimeTextureOverride->resourceId == input.resourceId)
				texture.shaderResourceView = runtimeTextureOverride->shaderResourceView;
			else if (!RenderPassTexturePool::GetInputTexture(input.resourceId, input.temporalView, texture, input.allowCrossAnchor) &&
					 input.temporalView == ShaderResource::TemporalView::Previous)
				RenderPassTexturePool::GetHistoryBootstrapTexture(device, input.resourceId, texture);

			if (!texture.shaderResourceView.ptr && !input.optional)
			{
				outError = "Runtime shader resource is unavailable: " + input.resourceId;
				return false;
			}
		}

		if (slot.cachedRootSignature != gameState.rootSignature ||
			slot.cachedMaximumTrackedDescriptors != renderPass.maximumTrackedDescriptors)
		{
			slot.cachedRootSignature = gameState.rootSignature;
			slot.cachedMaximumTrackedDescriptors = renderPass.maximumTrackedDescriptors;
			slot.layouts.clear();
			slot.graphicsBindingLocations.clear();
			slot.computeBindingLocations.clear();
			slot.resourceTablePool.Reset();
			slot.samplerTablePool.Reset();
			slot.boundRootTables.clear();

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

		const bool requiresResourceHeap = hasShaderInput || hasRuntimeUnorderedAccess;

		const bool requiresSamplerHeap = !renderPass.samplers.empty();
		UINT totalDescriptors = 0;
		UINT totalSamplerDescriptors = 0;

		for (const auto& binding : gameState.rootBindings)
		{
			if (binding.type != RenderPassMipChain::RootArgumentType::DescriptorTable)
				continue;

			const auto layoutIt = std::find_if(slot.layouts.begin(), slot.layouts.end(), [&](const auto& layout)
											   { return layout.rootParameterIndex == binding.rootParameterIndex; });

			if (layoutIt == slot.layouts.end())
				continue;

			const auto* sourceHeap = FindHeap(gameState, {binding.value}, layoutIt->heapType);

			if (!sourceHeap)
				continue;

			const UINT64 byteOffset = binding.value - sourceHeap->gpuStart.ptr;
			const UINT64 heapBytes = static_cast<UINT64>(sourceHeap->descriptorCount) * sourceHeap->descriptorIncrementSize;

			if (byteOffset >= heapBytes)
				continue;

			//the root layout defines the destination span; the heap bounds define how much source can be copied.
			//unbounded ranges remain capped by maximumTrackedDescriptors through the resolved layout.
			const UINT descriptorCount = layoutIt->descriptorCount;
			const UINT64 availableDescriptorCount = (heapBytes - byteOffset) / sourceHeap->descriptorIncrementSize;
			const UINT sourceDescriptorCount = static_cast<UINT>((std::min)(availableDescriptorCount, static_cast<UINT64>(descriptorCount)));

			if (!descriptorCount || !sourceDescriptorCount)
				continue;

			ActiveTable table{};
			table.rootParameterIndex = binding.rootParameterIndex;
			table.heapType = layoutIt->heapType;
			table.descriptorCount = descriptorCount;
			table.sourceDescriptorCount = sourceDescriptorCount;
			table.canReuseBinding = layoutIt->canReuseBinding;
			table.originalGpu = {binding.value};
			table.originalCpu = {sourceHeap->cpuStart.ptr + byteOffset};

			if (sourceDescriptorCount < descriptorCount)
			{
				auto* loggedPartialTable = &gLoggedPartialResourceTable;

				if (table.heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
					loggedPartialTable = &gLoggedPartialSamplerTable;

				if (!loggedPartialTable->exchange(true, std::memory_order_relaxed))
					ShaderInjectorIO::WriteToLogFile("ShaderResourceRuntime->BindResources: extending short game descriptor table pass=" + renderPass.name + " rootParameter=" + std::to_string(table.rootParameterIndex) + " sourceDescriptors=" + std::to_string(sourceDescriptorCount) + " destinationDescriptors=" + std::to_string(descriptorCount));
			}

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

				totalSamplerDescriptors += table.descriptorCount;
			}

			slot.activeTables.push_back(table);
		}

		if (!slot.descriptorIncrementSize)
			slot.descriptorIncrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		if (!slot.samplerDescriptorIncrementSize)
			slot.samplerDescriptorIncrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

		const UINT increment = slot.descriptorIncrementSize;
		const UINT samplerIncrement = slot.samplerDescriptorIncrementSize;

		//retain scratch capacity between passes so batching does not introduce per-draw allocations.
		thread_local std::vector<DescriptorOverride> descriptorOverrides;
		thread_local std::vector<SamplerOverride> samplerOverrides;
		descriptorOverrides.clear();
		samplerOverrides.clear();
		descriptorOverrides.reserve(renderPass.inputs.size() + renderPass.outputs.size());
		samplerOverrides.reserve(renderPass.samplers.size());

		thread_local std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> overrideCopyDestinations;
		thread_local std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> overrideCopySources;
		overrideCopyDestinations.clear();
		overrideCopySources.clear();
		const size_t maximumOverrideCount = renderPass.inputs.size() + renderPass.outputs.size();
		overrideCopyDestinations.reserve(maximumOverrideCount);
		overrideCopySources.reserve(maximumOverrideCount);
		const auto resolveDescriptorDestination = [&](D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
													  uint32_t shaderRegister,
													  uint32_t registerSpace,
													  size_t& outTableIndex,
													  UINT& outTableOffset) -> bool
		{
			const bool samplerDescriptor = rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
			D3D12_DESCRIPTOR_HEAP_TYPE requiredHeapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			char registerPrefix = 't';
			const char* bindingName = "SRV";
			if (rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV)
			{
				registerPrefix = 'u';
				bindingName = "UAV";
			}
			if (samplerDescriptor)
			{
				requiredHeapType = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
				registerPrefix = 's';
				bindingName = "sampler";
			}
			const uint64_t bindingKey = (static_cast<uint64_t>(rangeType) << 56) |
										((static_cast<uint64_t>(registerSpace) & 0x0FFFFFFFull) << 28) |
										(static_cast<uint64_t>(shaderRegister) & 0x0FFFFFFFull);
			auto* bindingLocations = &slot.graphicsBindingLocations;
			if (computePipeline)
				bindingLocations = &slot.computeBindingLocations;
			auto locationIt = bindingLocations->find(bindingKey);

			if (locationIt == bindingLocations->end())
			{
				std::vector<RenderPassResourceRegistry::DescriptorBindingLocation> locations;
				D3D12_SHADER_VISIBILITY shaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
				if (computePipeline)
					shaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

				if (!RenderPassResourceRegistry::GetDescriptorBindingCandidates(
						gameState.rootSignature,
						rangeType,
						shaderRegister,
						registerSpace,
						renderPass.maximumTrackedDescriptors,
						shaderVisibility,
						locations))
				{
					outError = "No " + std::string(bindingName) +
							   " root binding exists for " + registerPrefix + std::to_string(shaderRegister) +
							   ", space" + std::to_string(registerSpace) + ".";
					return false;
				}

				locationIt = bindingLocations->emplace(bindingKey, std::move(locations)).first;
			}

			const RenderPassResourceRegistry::DescriptorBindingLocation* location = nullptr;
			const ActiveTable* activeTable = nullptr;

			for (const auto& candidate : locationIt->second)
			{
				const auto tableIt = std::find_if(slot.activeTables.begin(), slot.activeTables.end(), [&](const auto& table)
												  { return table.rootParameterIndex == candidate.rootParameterIndex &&
														   table.heapType == requiredHeapType &&
														   candidate.tableOffset < table.descriptorCount; });
				if (tableIt != slot.activeTables.end())
				{
					location = &candidate;
					activeTable = &*tableIt;
					break;
				}
			}

			if (!location || !activeTable)
			{
				//custom passes may need UAVs or dynamic samplers that the game draw never binds.
				//UAVs require complete explicit coverage; sampler tables can initialize unused slots to valid defaults.
				if ((rangeType == D3D12_DESCRIPTOR_RANGE_TYPE_UAV || samplerDescriptor) && !locationIt->second.empty())
				{
					const auto& candidate = locationIt->second.front();
					const auto layoutIterator = std::find_if(slot.layouts.begin(), slot.layouts.end(), [&](const auto& layout) { return layout.rootParameterIndex == candidate.rootParameterIndex; });

					if (layoutIterator != slot.layouts.end())
					{
						ActiveTable outputTable{};
						outputTable.rootParameterIndex = candidate.rootParameterIndex;
						outputTable.heapType = requiredHeapType;
						outputTable.descriptorCount = layoutIterator->descriptorCount;
						slot.activeTables.push_back(outputTable);
						if (samplerDescriptor)
						{
							totalSamplerDescriptors += outputTable.descriptorCount;

							if (!gLoggedIndependentSamplerTable.exchange(true, std::memory_order_relaxed))
								ShaderInjectorIO::WriteToLogFile("ShaderResourceRuntime->BindResources: supplying independent sampler table pass=" + renderPass.name + " descriptors=" + std::to_string(outputTable.descriptorCount));
						}
						else
							totalDescriptors += outputTable.descriptorCount;
						location = &candidate;
						activeTable = &slot.activeTables.back();
					}
				}
			}

			if (!location || !activeTable)
			{
				outError = "No compatible active " + std::string(bindingName) +
						   " table exists for " + registerPrefix + std::to_string(shaderRegister) + ", space" +
						   std::to_string(registerSpace) + " on this draw or dispatch.";
				return false;
			}

			outTableIndex = static_cast<size_t>(activeTable - slot.activeTables.data());
			outTableOffset = location->tableOffset;
			slot.activeTables[outTableIndex].hasOverrides = true;

			return true;
		};

		const auto bindDescriptor = [&](D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
										uint32_t shaderRegister,
										uint32_t registerSpace,
										D3D12_CPU_DESCRIPTOR_HANDLE sourceDescriptor) -> bool
		{
			size_t tableIndex = 0;
			UINT tableOffset = 0;
			if (!resolveDescriptorDestination(
					rangeType,
					shaderRegister,
					registerSpace,
					tableIndex,
					tableOffset))
			{
				return false;
			}

			//coalesce duplicate destinations to preserve last-override-wins and avoid overlapping copy ranges.
			const auto existingDestination = std::find_if(
				descriptorOverrides.begin(),
				descriptorOverrides.end(),
				[&](const DescriptorOverride& queuedOverride)
				{
					return queuedOverride.tableIndex == tableIndex && queuedOverride.tableOffset == tableOffset;
				});
			if (existingDestination != descriptorOverrides.end())
			{
				existingDestination->source = sourceDescriptor;
				return true;
			}

			descriptorOverrides.push_back({tableIndex, tableOffset, sourceDescriptor});
			return true;
		};

		for (size_t inputIndex = 0; inputIndex < renderPass.inputs.size(); ++inputIndex)
		{
			const auto& input = renderPass.inputs[inputIndex];
			if (input.access != RenderPass::ResourceAccess::ShaderResource)
			{
				continue;
			}
			if (input.origin == ShaderResource::ResourceOrigin::Disk)
			{
				const ShaderResource::TextureDisk* disk = DatabaseShaderResources::FindShaderResourceById(input.resourceId);
				if (!disk)
				{
					if (input.optional)
						continue;
					outError = "Imported DDS is missing: " + input.resourceId;
					return false;
				}
				TextureGPU* texture = nullptr;
				const auto cachedTextureIt = slot.resolvedTextures.find(input.resourceId);
				if (cachedTextureIt != slot.resolvedTextures.end())
					texture = cachedTextureIt->second;
				else
				{
					texture = GetOrCreateTexture(device, commandList, *disk, outError);
					if (texture)
						slot.resolvedTextures.emplace(input.resourceId, texture);
				}
				if (!texture)
				{
					if (input.optional)
					{
						outError.clear();
						continue;
					}
					return false;
				}
				if (!bindDescriptor(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, input.shaderRegister, input.registerSpace,
					texture->shaderResourceViewHeap->GetCPUDescriptorHandleForHeapStart()))
					return false;
				continue;
			}
			if (input.origin != ShaderResource::ResourceOrigin::Runtime)
				continue;

			const D3D12_CPU_DESCRIPTOR_HANDLE sourceDescriptor = runtimeInputs[inputIndex].shaderResourceView;

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
			if (runtimeTextureOverride && runtimeTextureOverride->unorderedAccessResourceId == output.resourceId)
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
			size_t tableIndex = 0;
			UINT tableOffset = 0;

			if (!resolveDescriptorDestination(
					D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,
					sampler.shaderRegister,
					sampler.registerSpace,
					tableIndex,
					tableOffset))
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
			description.ComparisonFunc = static_cast<D3D12_COMPARISON_FUNC>(sampler.comparisonFunction);
			std::copy(sampler.borderColor.begin(), sampler.borderColor.end(), description.BorderColor);
			description.MinLOD = sampler.minimumLod;
			description.MaxLOD = (std::max)(sampler.minimumLod, sampler.maximumLod);
			const auto existingSampler = std::find_if(samplerOverrides.begin(), samplerOverrides.end(), [&](const SamplerOverride& candidate) { return candidate.tableIndex == tableIndex && candidate.tableOffset == tableOffset; });

			if (existingSampler == samplerOverrides.end())
				samplerOverrides.push_back({tableIndex, tableOffset, description});
			else
				existingSampler->description = description;
		}

		for (size_t tableIndex = 0; tableIndex < slot.activeTables.size(); ++tableIndex)
		{
			const auto& table = slot.activeTables[tableIndex];

			if (table.originalCpu.ptr || table.heapType == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
				continue;

			const size_t overrideCount = std::count_if(descriptorOverrides.begin(), descriptorOverrides.end(), [&](const auto& descriptorOverride) { return descriptorOverride.tableIndex == tableIndex; });

			if (overrideCount != table.descriptorCount)
			{
				outError = "A new UAV table must have every descriptor explicitly supplied by the custom pass.";
				return false;
			}
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

		if (requiresResourceHeap && !PrepareChainTables(slot, device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, slot.resourceTablePool, totalDescriptors, samplerOverrides, outError))
			return false;

		if (requiresSamplerHeap && !PrepareChainTables(slot, device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, slot.samplerTablePool, totalSamplerDescriptors, samplerOverrides, outError))
			return false;

		for (const SamplerOverride& samplerOverride : samplerOverrides)
		{
			//cached sampler tables are immutable, even if rewriting the same bytes would look harmless.
			if (!slot.activeTables[samplerOverride.tableIndex].needsCopy)
				continue;

			D3D12_CPU_DESCRIPTOR_HANDLE destination = slot.activeTables[samplerOverride.tableIndex].allocation.cpuStart;
			destination.ptr += static_cast<SIZE_T>(samplerOverride.tableOffset) * samplerIncrement;
			device->CreateSampler(&samplerOverride.description, destination);
		}

		for (const DescriptorOverride& descriptorOverride : descriptorOverrides)
		{
			D3D12_CPU_DESCRIPTOR_HANDLE destination = slot.activeTables[descriptorOverride.tableIndex].allocation.cpuStart;
			destination.ptr += static_cast<SIZE_T>(descriptorOverride.tableOffset) * increment;
			overrideCopyDestinations.push_back(destination);
			overrideCopySources.push_back(descriptorOverride.source);
		}

		if (!overrideCopyDestinations.empty())
		{
			{
				PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Timing::ApplyShaderResourceOverrides);
				device->CopyDescriptors(
					static_cast<UINT>(overrideCopyDestinations.size()),
					overrideCopyDestinations.data(),
					nullptr,
					static_cast<UINT>(overrideCopySources.size()),
					overrideCopySources.data(),
					nullptr,
					D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			}
			PerformanceMetrics::Increment(
				PerformanceMetrics::Counter::ShaderResourceOverridesCopied,
				overrideCopySources.size());
			PerformanceMetrics::Increment(PerformanceMetrics::Counter::ShaderResourceCopyBatches);
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

		ID3D12DescriptorHeap* resourceHeap = originalResourceHeap;
		ID3D12DescriptorHeap* samplerHeap = originalSamplerHeap;
		if (requiresResourceHeap)
			resourceHeap = slot.resourceTablePool.heap;
		if (requiresSamplerHeap)
			samplerHeap = slot.samplerTablePool.heap;

		std::array<ID3D12DescriptorHeap*, 2> heaps{};

		UINT heapCount = 0;

		if (resourceHeap)
			heaps[heapCount++] = resourceHeap;

		if (samplerHeap)
			heaps[heapCount++] = samplerHeap;

		ID3D12DescriptorHeap* const* heapData = nullptr;
		if (heapCount)
			heapData = heaps.data();
		const bool heapsUnchanged = bindingBatchActive && slot.boundDescriptorHeapsKnown && slot.boundDescriptorHeapCount == heapCount && std::equal(heaps.begin(), heaps.begin() + heapCount, slot.boundDescriptorHeaps.begin());

		if (heapsUnchanged)
			PerformanceMetrics::Increment(PerformanceMetrics::Counter::ShaderResourceHeapBindAvoided);
		else
		{
			commandList->SetDescriptorHeaps(heapCount, heapData);
			slot.boundRootTables.clear();

			PerformanceMetrics::Increment(PerformanceMetrics::Counter::ShaderResourceHeapBind);
		}

		slot.boundDescriptorHeaps = heaps;

		slot.boundDescriptorHeapCount = heapCount;

		slot.boundDescriptorHeapsKnown = bindingBatchActive;

		for (const ActiveTable& table : slot.activeTables)
		{
			D3D12_GPU_DESCRIPTOR_HANDLE handle = table.originalGpu;

			if (table.allocation.heap)
				handle = table.allocation.gpuStart;

			const auto previousBinding = std::find_if(slot.boundRootTables.begin(), slot.boundRootTables.end(), [&](const RootTableRestore& binding)
			{
				return binding.rootParameterIndex == table.rootParameterIndex && binding.computePipeline == computePipeline;
			});

			//static-data ranges need their binding point reasserted even when the descriptor bytes are shared.
			if (table.canReuseBinding && heapsUnchanged && previousBinding != slot.boundRootTables.end() && previousBinding->descriptorHandle.ptr == handle.ptr)
			{
				PerformanceMetrics::Increment(PerformanceMetrics::Counter::ShaderResourceRootTableBindAvoided);
				continue;
			}

			if (computePipeline)
				commandList->SetComputeRootDescriptorTable(table.rootParameterIndex, handle);
			else
				commandList->SetGraphicsRootDescriptorTable(table.rootParameterIndex, handle);

			if (previousBinding == slot.boundRootTables.end())
				slot.boundRootTables.push_back({table.rootParameterIndex, handle, computePipeline});
			else
				previousBinding->descriptorHandle = handle;
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
					slot.restoreRootTables.push_back({binding.rootParameterIndex,
													  {binding.value},
													  restoreComputePipeline});
				}
			}
		};

		appendRootTables(gameState.rootBindings, computePipeline);
		appendRootTables(oppositePipelineState.rootBindings, !computePipeline);
		slot.pendingRestore = true;
		PerformanceMetrics::Increment(PerformanceMetrics::Counter::ShaderResourceBindSucceeded);
		return true;
	}

	ScopedResourceBindingBatch::ScopedResourceBindingBatch(ID3D12GraphicsCommandList* commandList)
		: commandList(commandList), previousCommandList(gResourceBindingBatchCommandList)
	{
		gResourceBindingBatchCommandList = commandList;

		if (CommandListSlot* slot = FindCommandListSlot(commandList))
		{
			slot->resourceTablePool.Reset();
			slot->samplerTablePool.Reset();
			slot->boundRootTables.clear();
		}
	}

	ScopedResourceBindingBatch::~ScopedResourceBindingBatch()
	{
		gResourceBindingBatchCommandList = previousCommandList;

		if (!keepResourcesForOriginalDraw)
		{
			HookD3D12::ScopedRenderPassInjection injectionScope;

			RestoreResourcesImmediately(commandList);
		}
	}

	void ScopedResourceBindingBatch::KeepResourcesForOriginalDraw()
	{
		keepResourcesForOriginalDraw = true;
	}

	void LoadTextures(ID3D12Device* device)
	{
		if (!device)
			return;

		const std::vector<ShaderResource::TextureDisk> disks = DatabaseShaderResources::CopyShaderResources();
		std::vector<const ShaderResource::TextureDisk*> pending;
		{
			std::lock_guard<std::mutex> lock(gTextureMutex);
			const auto deviceTextures = gTextures.find(device);
			std::unordered_set<std::string> pendingIds;
			for (const ShaderResource::TextureDisk& disk : disks)
			{
				//each texture is created and transitioned once; a second barrier would name the wrong state.
				if (!disk.validationError.empty() || !pendingIds.insert(disk.id).second)
					continue;
				if (deviceTextures != gTextures.end())
				{
					const auto existing = deviceTextures->second.find(disk.id);
					if (existing != deviceTextures->second.end() && existing->second->texture)
						continue;
				}
				pending.push_back(&disk);
			}
		}
		if (pending.empty())
			return;

		//one private queue submission for every texture, waited for here, so no game command list records an upload.
		HookD3D12::ScopedRenderPassInjection injectionScope;
		D3D12_COMMAND_QUEUE_DESC queueDescription{};
		queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		ComPtr<ID3D12CommandQueue> queue;
		ComPtr<ID3D12CommandAllocator> allocator;
		ComPtr<ID3D12GraphicsCommandList> commandList;
		ComPtr<ID3D12Fence> fence;
		if (FAILED(device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue))) ||
			FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
			FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList))) ||
			FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
		{
			ShaderInjectorIO::WriteToLogFileError("ShaderResourceRuntime->LoadTextures: could not create the DDS upload queue");
			return;
		}

		std::vector<TextureGPU*> textures;
		for (const ShaderResource::TextureDisk* disk : pending)
		{
			std::string error;
			//a render pass may have created it meanwhile, with its upload in a game command list.
			{
				std::lock_guard<std::mutex> lock(gTextureMutex);
				const auto existing = gTextures[device].find(disk->id);
				if (existing != gTextures[device].end() && existing->second->texture)
					continue;
			}
			TextureGPU* texture = GetOrCreateTexture(device, commandList.Get(), *disk, error);
			if (!texture)
			{
				ShaderInjectorIO::WriteToLogFileWarning("ShaderResourceRuntime->LoadTextures: " + error);
				continue;
			}

			//the upload leaves the texture in a pixel/non-pixel shader-resource state; COMMON promotes on any queue.
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = texture->texture.Get();
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
			commandList->ResourceBarrier(1, &barrier);

			std::error_code fileError;
			const std::filesystem::path path = ShaderInjectorIO::PathFromUTF8(disk->filePath);
			texture->fileTime = static_cast<uint64_t>(std::filesystem::last_write_time(path, fileError).time_since_epoch().count());
			texture->fileSize = static_cast<uint64_t>(std::filesystem::file_size(path, fileError));
			textures.push_back(texture);
		}

		if (FAILED(commandList->Close()))
		{
			ShaderInjectorIO::WriteToLogFileError("ShaderResourceRuntime->LoadTextures: could not close the DDS upload list");
			return;
		}

		ID3D12CommandList* lists[] = {commandList.Get()};
		queue->ExecuteCommandLists(1, lists);
		HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		const bool signalled = event && SUCCEEDED(queue->Signal(fence.Get(), 1)) && SUCCEEDED(fence->SetEventOnCompletion(1, event));
		if (signalled)
			WaitForSingleObject(event, INFINITE);
		if (event)
			CloseHandle(event);
		if (!signalled || fence->GetCompletedValue() < 1)
		{
			ShaderInjectorIO::WriteToLogFileError("ShaderResourceRuntime->LoadTextures: the DDS upload did not complete");
			return;
		}

		std::lock_guard<std::mutex> lock(gTextureMutex);
		for (TextureGPU* texture : textures)
		{
			texture->upload.Reset();
			texture->uploaded = true;
		}
	}

	void ReloadTextures()
	{
		DatabaseShaderResources::RefreshShaderResources();

		//retire textures whose file changed or disappeared; LoadTextures then uploads the new and changed ones.
		std::vector<ID3D12Device*> devices;
		{
			std::lock_guard<std::mutex> lock(gTextureMutex);
			for (auto& [device, textures] : gTextures)
			{
				devices.push_back(device);
				for (auto texture = textures.begin(); texture != textures.end();)
				{
					const ShaderResource::TextureDisk* disk = DatabaseShaderResources::FindShaderResourceById(texture->first);
					std::error_code fileError;
					const std::filesystem::path path = disk ? ShaderInjectorIO::PathFromUTF8(disk->filePath) : std::filesystem::path();
					const bool unchanged = disk && disk->validationError.empty() && texture->second->uploaded &&
										   texture->second->fileTime == static_cast<uint64_t>(std::filesystem::last_write_time(path, fileError).time_since_epoch().count()) &&
										   texture->second->fileSize == static_cast<uint64_t>(std::filesystem::file_size(path, fileError));
					if (unchanged)
					{
						++texture;
						continue;
					}
					gRetiredTextures.push_back(std::move(texture->second));
					texture = textures.erase(texture);
				}
			}
		}

		for (ID3D12Device* device : devices)
			LoadTextures(device);
		gTextureGeneration.fetch_add(1, std::memory_order_acq_rel);
	}

	bool FindTextureShaderResourceView(ID3D12Device* device, const std::string& resourceId, D3D12_CPU_DESCRIPTOR_HANDLE& outShaderResourceView)
	{
		std::lock_guard<std::mutex> lock(gTextureMutex);
		const auto deviceTextures = gTextures.find(device);
		if (deviceTextures == gTextures.end())
			return false;
		const auto texture = deviceTextures->second.find(resourceId);
		if (texture == deviceTextures->second.end() || !texture->second->uploaded)
			return false;
		outShaderResourceView = texture->second->shaderResourceViewHeap->GetCPUDescriptorHandleForHeapStart();
		return true;
	}

	void RestoreResources(ID3D12GraphicsCommandList* commandList)
	{
		if (gResourceBindingBatchCommandList == commandList)
		{
			CommandListSlot* slot = FindCommandListSlot(commandList);

			if (slot && slot->pendingRestore)
				PerformanceMetrics::Increment(PerformanceMetrics::Counter::ShaderResourceRestoreDeferred);

			return;
		}

		RestoreResourcesImmediately(commandList);
	}

	void RestoreResourcesImmediately(ID3D12GraphicsCommandList* commandList)
	{
		CommandListSlot* slot = FindCommandListSlot(commandList);

		if (!slot)
			return;

		//a mip generator or a pass using native game bindings can change heaps outside this batch.
		slot->boundDescriptorHeapsKnown = false;
		slot->resourceTablePool.Reset();
		slot->samplerTablePool.Reset();
		slot->boundRootTables.clear();

		if (!slot->pendingRestore)
			return;

		PerformanceMetrics::ScopedTimer restoreTimer(PerformanceMetrics::Timing::RestoreShaderResources);

		ID3D12DescriptorHeap* const* restoreHeapData = nullptr;
		if (!slot->restoreHeaps.empty())
			restoreHeapData = slot->restoreHeaps.data();
		commandList->SetDescriptorHeaps(static_cast<UINT>(slot->restoreHeaps.size()), restoreHeapData);

		PerformanceMetrics::Increment(PerformanceMetrics::Counter::ShaderResourceHeapRestore);

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
		commandListSlot.boundDescriptorHeapsKnown = false;
		commandListSlot.resourceTablePool.Reset();
		commandListSlot.samplerTablePool.Reset();
		commandListSlot.boundRootTables.clear();

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
			ID3D12GraphicsCommandList* graphicsCommandList = reinterpret_cast<ID3D12GraphicsCommandList*>(commandLists[commandListIndex]);

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

				ShaderInjectorIO::WriteToLogFileError("ShaderResourceRuntime->NotifyCommandListsSubmitted: could not create the retirement fence");
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

			ShaderInjectorIO::WriteToLogFileError("ShaderResourceRuntime->NotifyCommandListsSubmitted: queue signal failed with " + StringHelper::FormatHRESULT(result));
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
		gResourceBindingBatchCommandList = nullptr;
	}

	void LogPerformanceStatistics()
	{
		PerformanceStatistics statistics;
		{
			std::lock_guard<std::mutex> lock(gCommandListSlotMutex);
			statistics.commandLists = gCommandListSlots.size();

			for (const auto& commandList : gCommandListSlots)
			{
				statistics.executionSlots += commandList.second->executionSlots.size();

				for (const auto& slot : commandList.second->executionSlots)
				{
					if (slot->retirementBlocked)
						++statistics.blockedSlots;
					statistics.heapPages += slot->pages.size() + slot->samplerPages.size();

					for (const auto& page : slot->pages)
						statistics.descriptorCapacity += page.capacity;

					for (const auto& page : slot->samplerPages)
						statistics.descriptorCapacity += page.capacity;
				}
			}
		}
		{
			std::lock_guard<std::mutex> lock(gPerformanceStatisticsMutex);
			gLastPerformanceStatistics = statistics;
		}

		ShaderInjectorIO::WriteToLogFile(StringHelper::Format(
			"ShaderResourceRuntime->Performance: commandLists=%zu executionSlots=%zu blockedSlots=%zu heapPages=%zu descriptorCapacity=%llu",
			statistics.commandLists, statistics.executionSlots, statistics.blockedSlots, statistics.heapPages, static_cast<unsigned long long>(statistics.descriptorCapacity)));
	}

	PerformanceStatistics GetLastPerformanceStatistics()
	{
		std::lock_guard<std::mutex> lock(gPerformanceStatisticsMutex);
		return gLastPerformanceStatistics;
	}
} //namespace ShaderResourceRuntime
