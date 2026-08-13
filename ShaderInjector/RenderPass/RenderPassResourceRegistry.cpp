#include "RenderPassResourceRegistry.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "Performance/PerformanceMetrics.h"

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
		std::shared_mutex gDescriptorHeapMutex;
		std::shared_mutex gResourceMutex;
		std::shared_mutex gRootSignatureMutex;
		using DescriptorRecord = const RenderPass::ResourceBindingDiagnostic*;
		// Four-kilobyte metadata pages keep unrelated command-recording threads from
		// contending on the same lock while retaining cache-friendly linear copies.
		constexpr size_t DescriptorPageSize = 512;

		template <typename Value>
		void HashDescriptorField(size_t& seed, const Value& value)
		{
			seed ^= std::hash<Value>{}(value) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
		}

		struct DescriptorMetadataHash
		{
			size_t operator()(const RenderPass::ResourceBindingDiagnostic& value) const
			{
				size_t seed = 0;
				HashDescriptorField(seed, value.pipeline);
				HashDescriptorField(seed, value.bindingType);
				HashDescriptorField(seed, value.rootParameterIndex);
				HashDescriptorField(seed, value.gpuAddress);
				HashDescriptorField(seed, value.gpuDescriptorHandle);
				HashDescriptorField(seed, value.descriptorHeapType);
				HashDescriptorField(seed, value.descriptorIndex);
				HashDescriptorField(seed, value.descriptorCount);
				HashDescriptorField(seed, value.descriptorViewDimension);
				HashDescriptorField(seed, value.descriptorMostDetailedMip);
				HashDescriptorField(seed, value.descriptorMipLevels);
				HashDescriptorField(seed, value.descriptorShader4ComponentMapping);
				HashDescriptorField(seed, value.descriptorPlaneSlice);
				HashDescriptorField(seed, value.descriptorResourceMinLodClamp);
				HashDescriptorField(seed, value.shaderRegister);
				HashDescriptorField(seed, value.registerSpace);
				HashDescriptorField(seed, value.destinationOffset);
				HashDescriptorField(seed, value.resourcePointer);
				HashDescriptorField(seed, value.resourceName);
				HashDescriptorField(seed, value.resourceDimension);
				HashDescriptorField(seed, value.resourceWidth);
				HashDescriptorField(seed, value.resourceHeight);
				HashDescriptorField(seed, value.resourceDepthOrArraySize);
				HashDescriptorField(seed, value.resourceMipLevels);
				HashDescriptorField(seed, value.resourceFormat);
				HashDescriptorField(seed, value.resourceSampleCount);
				HashDescriptorField(seed, value.resourceSampleQuality);
				HashDescriptorField(seed, value.bufferOffset);
				HashDescriptorField(seed, value.bufferSize);
				HashDescriptorField(seed, value.firstElement);
				HashDescriptorField(seed, value.elementCount);
				HashDescriptorField(seed, value.structureByteStride);
				for (uint32_t rootConstant : value.rootConstants)
					HashDescriptorField(seed, rootConstant);
				return seed;
			}
		};

		struct DescriptorMetadataEqual
		{
			bool operator()(
				const RenderPass::ResourceBindingDiagnostic& left,
				const RenderPass::ResourceBindingDiagnostic& right) const
			{
				return std::tie(
					left.pipeline,
					left.bindingType,
					left.rootParameterIndex,
					left.gpuAddress,
					left.gpuDescriptorHandle,
					left.descriptorHeapType,
					left.descriptorIndex,
					left.descriptorCount,
					left.descriptorViewDimension,
					left.descriptorMostDetailedMip,
					left.descriptorMipLevels,
					left.descriptorShader4ComponentMapping,
					left.descriptorPlaneSlice,
					left.descriptorResourceMinLodClamp,
					left.shaderRegister,
					left.registerSpace,
					left.destinationOffset,
					left.resourcePointer,
					left.resourceName,
					left.resourceDimension,
					left.resourceWidth,
					left.resourceHeight,
					left.resourceDepthOrArraySize,
					left.resourceMipLevels,
					left.resourceFormat,
					left.resourceSampleCount,
					left.resourceSampleQuality,
					left.bufferOffset,
					left.bufferSize,
					left.firstElement,
					left.elementCount,
					left.structureByteStride,
					left.rootConstants) ==
					std::tie(
						right.pipeline,
						right.bindingType,
						right.rootParameterIndex,
						right.gpuAddress,
						right.gpuDescriptorHandle,
						right.descriptorHeapType,
						right.descriptorIndex,
						right.descriptorCount,
						right.descriptorViewDimension,
						right.descriptorMostDetailedMip,
						right.descriptorMipLevels,
						right.descriptorShader4ComponentMapping,
						right.descriptorPlaneSlice,
						right.descriptorResourceMinLodClamp,
						right.shaderRegister,
						right.registerSpace,
						right.destinationOffset,
						right.resourcePointer,
						right.resourceName,
						right.resourceDimension,
						right.resourceWidth,
						right.resourceHeight,
						right.resourceDepthOrArraySize,
						right.resourceMipLevels,
						right.resourceFormat,
						right.resourceSampleCount,
						right.resourceSampleQuality,
						right.bufferOffset,
						right.bufferSize,
						right.firstElement,
						right.elementCount,
						right.structureByteStride,
						right.rootConstants);
			}
		};

		struct DescriptorPage
		{
			std::array<std::atomic<DescriptorRecord>, DescriptorPageSize> descriptors;
			std::atomic<uint32_t> trackedDescriptorCount{ 0 };

			DescriptorPage()
			{
				for (auto& descriptor : descriptors)
					descriptor.store(nullptr, std::memory_order_relaxed);
			}
		};

		struct DescriptorHeapRecord
		{
			ID3D12DescriptorHeap* identity = nullptr;
			SIZE_T start = 0;
			SIZE_T end = 0;
			UINT descriptorCount = 0;
			UINT descriptorIncrementSize = 0;
			D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
			std::unique_ptr<std::atomic<DescriptorPage*>[]> pages;
			size_t pageCount = 0;
			std::atomic<size_t> trackedDescriptorCount{ 0 };
			std::atomic<bool> active{ true };

			~DescriptorHeapRecord()
			{
				if (!pages)
					return;
				for (size_t pageIndex = 0; pageIndex < pageCount; ++pageIndex)
					delete pages[pageIndex].load(std::memory_order_relaxed);
			}
		};

		struct DescriptorHeapLookupCache
		{
			uint64_t generation = 0;
			size_t nextEntry = 0;
			std::array<DescriptorHeapRecord*, 4> entries{};
		};

		std::map<SIZE_T, std::shared_ptr<DescriptorHeapRecord>> gDescriptorHeaps;
		// Heap address ranges can be recycled while other command-recording threads
		// finish a copy. Retaining inactive records makes cached raw lookups safe
		// without paying shared_ptr reference-count traffic on every descriptor copy.
		std::vector<std::shared_ptr<DescriptorHeapRecord>> gRetiredDescriptorHeaps;
		std::atomic<uint64_t> gDescriptorHeapGeneration{ 1 };
		std::atomic<size_t> gFallbackTrackedDescriptorCount{ 0 };
		// Descriptors created before the heap hook was installed remain supported here.
		// Once heaps are registered, normal gameplay traffic uses the indexed pages above.
		std::map<SIZE_T, DescriptorRecord> gDescriptors;
		std::mutex gDescriptorMetadataMutex;
		std::unordered_set<
			RenderPass::ResourceBindingDiagnostic,
			DescriptorMetadataHash,
			DescriptorMetadataEqual> gDescriptorMetadata;
		std::map<uint64_t, RenderPass::ResourceBindingDiagnostic> gResourcesByGpuAddress;
		std::unordered_map<ID3D12RootSignature*, RootSignatureLayout> gRootSignatures;
		constexpr size_t DescriptorBloomWordCount = 1u << 14;
		constexpr UINT MaximumBloomRangeScan = 64;
		std::array<std::atomic<uint64_t>, DescriptorBloomWordCount> gDescriptorBloom{};
		struct DescriptorCopySegment
		{
			SIZE_T destinationStart = 0;
			SIZE_T sourceStart = 0;
			UINT descriptorCount = 0;
			DescriptorHeapRecord* destinationHeap = nullptr;
			DescriptorHeapRecord* sourceHeap = nullptr;
			SIZE_T destinationFirstDescriptor = 0;
			SIZE_T sourceFirstDescriptor = 0;
		};

		DescriptorRecord InternDescriptorMetadata(RenderPass::ResourceBindingDiagnostic binding)
		{
			// Handles and table locations belong to the descriptor slot, not the
			// immutable view metadata. Excluding them allows identical views copied to
			// millions of slots to share one stable record.
			binding.cpuDescriptorHandle = 0;
			binding.gpuDescriptorHandle = 0;
			binding.descriptorIndex = UINT32_MAX;

			const size_t metadataHash = DescriptorMetadataHash{}(binding);
			struct CachedMetadata
			{
				size_t hash = 0;
				DescriptorRecord record = nullptr;
			};
			thread_local std::array<CachedMetadata, 64> cache{};
			CachedMetadata& cached = cache[metadataHash % cache.size()];
			if (cached.record && cached.hash == metadataHash &&
				DescriptorMetadataEqual{}(*cached.record, binding))
			{
				return cached.record;
			}

			std::lock_guard<std::mutex> lock(gDescriptorMetadataMutex);
			if (gDescriptorMetadata.empty())
				gDescriptorMetadata.reserve(4096);
			const auto [metadataIt, inserted] = gDescriptorMetadata.emplace(std::move(binding));
			(void)inserted;
			cached = { metadataHash, &*metadataIt };
			return cached.record;
		}

		uint64_t MixDescriptorHandle(uint64_t value)
		{
			value ^= value >> 30;
			value *= 0xbf58476d1ce4e5b9ULL;
			value ^= value >> 27;
			value *= 0x94d049bb133111ebULL;
			return value ^ (value >> 31);
		}

		void MarkDescriptorPossiblyTracked(SIZE_T descriptor)
		{
			const uint64_t hash = MixDescriptorHandle(static_cast<uint64_t>(descriptor) >> 4);
			const size_t firstWord = static_cast<size_t>(hash) & (DescriptorBloomWordCount - 1);
			const size_t secondWord = static_cast<size_t>(hash >> 32) & (DescriptorBloomWordCount - 1);
			const uint64_t firstBit = 1ULL << ((hash >> 18) & 63);
			const uint64_t secondBit = 1ULL << ((hash >> 50) & 63);
			gDescriptorBloom[firstWord].fetch_or(firstBit, std::memory_order_release);
			gDescriptorBloom[secondWord].fetch_or(secondBit, std::memory_order_release);
		}

		bool DescriptorPossiblyTracked(SIZE_T descriptor)
		{
			const uint64_t hash = MixDescriptorHandle(static_cast<uint64_t>(descriptor) >> 4);
			const size_t firstWord = static_cast<size_t>(hash) & (DescriptorBloomWordCount - 1);
			const size_t secondWord = static_cast<size_t>(hash >> 32) & (DescriptorBloomWordCount - 1);
			const uint64_t firstBit = 1ULL << ((hash >> 18) & 63);
			const uint64_t secondBit = 1ULL << ((hash >> 50) & 63);
			return (gDescriptorBloom[firstWord].load(std::memory_order_acquire) & firstBit) != 0 &&
				(gDescriptorBloom[secondWord].load(std::memory_order_acquire) & secondBit) != 0;
		}

		bool DescriptorRangePossiblyTracked(
			SIZE_T start,
			UINT descriptorCount,
			UINT descriptorIncrementSize)
		{
			if (!descriptorCount)
				return false;
			// Large copies are uncommon and cheap to inspect with the ordered map. Avoid
			// turning a conservative prefilter into a long linear scan.
			if (descriptorCount > MaximumBloomRangeScan)
				return true;
			for (UINT descriptorIndex = 0; descriptorIndex < descriptorCount; ++descriptorIndex)
			{
				if (DescriptorPossiblyTracked(
					start + static_cast<SIZE_T>(descriptorIndex) * descriptorIncrementSize))
				{
					return true;
				}
			}
			return false;
		}

		bool DescriptorHeapRangeFits(
			const DescriptorHeapRecord& heap,
			SIZE_T start,
			UINT descriptorCount,
			UINT descriptorIncrementSize)
		{
			if (!descriptorCount || !heap.active.load(std::memory_order_acquire) ||
				!heap.descriptorIncrementSize || start < heap.start || start >= heap.end)
			{
				return false;
			}

			const SIZE_T byteOffset = start - heap.start;
			if (byteOffset % heap.descriptorIncrementSize != 0 ||
				(descriptorIncrementSize && descriptorIncrementSize != heap.descriptorIncrementSize))
			{
				return false;
			}

			const SIZE_T firstDescriptor = byteOffset / heap.descriptorIncrementSize;
			return firstDescriptor < heap.descriptorCount &&
				descriptorCount <= heap.descriptorCount - firstDescriptor;
		}

		DescriptorHeapRecord* FindDescriptorHeap(
			SIZE_T start,
			UINT descriptorCount = 1,
			UINT descriptorIncrementSize = 0)
		{
			thread_local DescriptorHeapLookupCache cache;
			const uint64_t generation = gDescriptorHeapGeneration.load(std::memory_order_acquire);
			if (cache.generation != generation)
			{
				cache.entries = {};
				cache.nextEntry = 0;
				cache.generation = generation;
			}

			for (DescriptorHeapRecord* cachedHeap : cache.entries)
			{
				if (cachedHeap && DescriptorHeapRangeFits(
					*cachedHeap,
					start,
					descriptorCount,
					descriptorIncrementSize))
				{
					return cachedHeap;
				}
			}

			DescriptorHeapRecord* heap = nullptr;
			{
				std::shared_lock<std::shared_mutex> lock(gDescriptorHeapMutex);
				auto heapIt = gDescriptorHeaps.upper_bound(start);
				if (heapIt != gDescriptorHeaps.begin())
				{
					--heapIt;
					if (heapIt->second && DescriptorHeapRangeFits(
						*heapIt->second,
						start,
						descriptorCount,
						descriptorIncrementSize))
					{
						cache.entries[cache.nextEntry] = heapIt->second.get();
						heap = heapIt->second.get();
						cache.nextEntry = (cache.nextEntry + 1) % cache.entries.size();
					}
				}
			}
			return heap;
		}

		DescriptorPage* GetDescriptorPage(
			DescriptorHeapRecord& heap,
			size_t pageIndex,
			bool create)
		{
			if (pageIndex >= heap.pageCount)
				return nullptr;
			DescriptorPage* page = heap.pages[pageIndex].load(std::memory_order_acquire);
			if (page || !create)
				return page;

			std::unique_ptr<DescriptorPage> newPage = std::make_unique<DescriptorPage>();
			DescriptorPage* expected = nullptr;
			if (heap.pages[pageIndex].compare_exchange_strong(
				expected,
				newPage.get(),
				std::memory_order_release,
				std::memory_order_acquire))
			{
				return newPage.release();
			}
			return expected;
		}

		DescriptorRecord ReadPageDescriptor(
			const DescriptorPage& page,
			size_t pageOffset)
		{
			return page.descriptors[pageOffset].load(std::memory_order_acquire);
		}

		void SetPageDescriptor(
			DescriptorHeapRecord& heap,
			DescriptorPage& page,
			size_t pageOffset,
			DescriptorRecord record)
		{
			DescriptorRecord previous = page.descriptors[pageOffset].exchange(
				record,
				std::memory_order_acq_rel);
			if (previous == record)
				return;
			const bool wasTracked = previous != nullptr;
			const bool isTracked = static_cast<bool>(record);
			if (wasTracked != isTracked)
			{
				if (isTracked)
				{
					page.trackedDescriptorCount.fetch_add(1, std::memory_order_relaxed);
					heap.trackedDescriptorCount.fetch_add(1, std::memory_order_relaxed);
				}
				else
				{
					page.trackedDescriptorCount.fetch_sub(1, std::memory_order_relaxed);
					heap.trackedDescriptorCount.fetch_sub(1, std::memory_order_relaxed);
				}
			}
		}

		void SetHeapDescriptor(
			DescriptorHeapRecord* heap,
			SIZE_T descriptor,
			DescriptorRecord record)
		{
			if (!heap)
				return;
			const SIZE_T descriptorIndex = (descriptor - heap->start) / heap->descriptorIncrementSize;
			const size_t pageIndex = static_cast<size_t>(descriptorIndex / DescriptorPageSize);
			const size_t pageOffset = static_cast<size_t>(descriptorIndex % DescriptorPageSize);
			DescriptorPage* page = GetDescriptorPage(*heap, pageIndex, static_cast<bool>(record));
			if (!page)
				return;
			if (heap->active.load(std::memory_order_relaxed))
				SetPageDescriptor(*heap, *page, pageOffset, record);
		}

		void DeactivateDescriptorHeap(const std::shared_ptr<DescriptorHeapRecord>& heap)
		{
			if (!heap)
				return;
			if (!heap->active.exchange(false, std::memory_order_acq_rel))
				return;
		}

		DescriptorRecord ReadDescriptorRecord(SIZE_T descriptor)
		{
			DescriptorHeapRecord* heap = FindDescriptorHeap(descriptor);
			if (heap)
			{
				const SIZE_T descriptorIndex = (descriptor - heap->start) / heap->descriptorIncrementSize;
				const size_t pageIndex = static_cast<size_t>(descriptorIndex / DescriptorPageSize);
				const size_t pageOffset = static_cast<size_t>(descriptorIndex % DescriptorPageSize);
				DescriptorPage* page = GetDescriptorPage(*heap, pageIndex, false);
				if (!page)
					return {};
				if (!heap->active.load(std::memory_order_relaxed))
					return {};
				return ReadPageDescriptor(*page, pageOffset);
			}

			std::shared_lock<std::shared_mutex> lock(gDescriptorMutex);
			const auto descriptorIt = gDescriptors.find(descriptor);
			return descriptorIt == gDescriptors.end() ? DescriptorRecord{} : descriptorIt->second;
		}

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
			DescriptorRecord record = InternDescriptorMetadata(std::move(binding));
			DescriptorHeapRecord* heap = FindDescriptorHeap(destination.ptr);
			if (heap)
			{
				SetHeapDescriptor(heap, destination.ptr, record);
				return;
			}

			MarkDescriptorPossiblyTracked(destination.ptr);
			std::unique_lock<std::shared_mutex> lock(gDescriptorMutex);
			const bool inserted = gDescriptors.insert_or_assign(
				destination.ptr,
				record).second;
			if (inserted)
				gFallbackTrackedDescriptorCount.fetch_add(1, std::memory_order_relaxed);
		}

		void EraseDescriptorIfTracked(D3D12_CPU_DESCRIPTOR_HANDLE destination)
		{
			DescriptorHeapRecord* heap = FindDescriptorHeap(destination.ptr);
			if (heap)
			{
				SetHeapDescriptor(heap, destination.ptr, {});
				return;
			}

			{
				std::shared_lock<std::shared_mutex> lock(gDescriptorMutex);
				if (gDescriptors.find(destination.ptr) == gDescriptors.end())
					return;
			}

			std::unique_lock<std::shared_mutex> lock(gDescriptorMutex);
			if (gDescriptors.erase(destination.ptr))
				gFallbackTrackedDescriptorCount.fetch_sub(1, std::memory_order_relaxed);
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

		void ResolveDescriptorCopySegment(
			DescriptorCopySegment& segment,
			UINT descriptorIncrementSize)
		{
			segment.sourceHeap = FindDescriptorHeap(
				segment.sourceStart,
				segment.descriptorCount,
				descriptorIncrementSize);
			if (segment.sourceHeap)
			{
				segment.sourceFirstDescriptor =
					(segment.sourceStart - segment.sourceHeap->start) /
					segment.sourceHeap->descriptorIncrementSize;
			}

			segment.destinationHeap = FindDescriptorHeap(
				segment.destinationStart,
				segment.descriptorCount,
				descriptorIncrementSize);
			if (segment.destinationHeap)
			{
				segment.destinationFirstDescriptor =
					(segment.destinationStart - segment.destinationHeap->start) /
					segment.destinationHeap->descriptorIncrementSize;
			}
		}

		bool DescriptorRangeMayContainTracked(
			SIZE_T start,
			UINT descriptorCount,
			UINT descriptorIncrementSize,
			DescriptorHeapRecord* heap,
			SIZE_T firstDescriptor)
		{
			if (heap)
			{
				if (!heap->active.load(std::memory_order_relaxed) ||
					!heap->trackedDescriptorCount.load(std::memory_order_relaxed))
					return false;
				const SIZE_T lastDescriptor = firstDescriptor + descriptorCount - 1;
				const size_t firstPage = static_cast<size_t>(firstDescriptor / DescriptorPageSize);
				const size_t lastPage = static_cast<size_t>(lastDescriptor / DescriptorPageSize);
				for (size_t pageIndex = firstPage; pageIndex <= lastPage; ++pageIndex)
				{
					DescriptorPage* page = GetDescriptorPage(*heap, pageIndex, false);
					if (page && page->trackedDescriptorCount.load(std::memory_order_relaxed))
						return true;
				}
				return false;
			}
			if (!gFallbackTrackedDescriptorCount.load(std::memory_order_relaxed))
				return false;
			return DescriptorRangePossiblyTracked(start, descriptorCount, descriptorIncrementSize);
		}

		void AppendDescriptorRange(
			SIZE_T start,
			UINT descriptorCount,
			UINT descriptorIncrementSize,
			DescriptorHeapRecord* heap,
			SIZE_T firstDescriptor,
			std::vector<DescriptorRecord>& records)
		{
			const size_t outputOffset = records.size();
			records.resize(outputOffset + descriptorCount);
			if (heap)
			{
				if (!heap->active.load(std::memory_order_relaxed) ||
					!heap->trackedDescriptorCount.load(std::memory_order_relaxed))
					return;
				UINT copiedDescriptorCount = 0;
				while (copiedDescriptorCount < descriptorCount)
				{
					const SIZE_T descriptorIndex = firstDescriptor + copiedDescriptorCount;
					const size_t pageIndex = static_cast<size_t>(descriptorIndex / DescriptorPageSize);
					const size_t pageOffset = static_cast<size_t>(descriptorIndex % DescriptorPageSize);
					const UINT descriptorsOnPage = (std::min)(
						descriptorCount - copiedDescriptorCount,
						static_cast<UINT>(DescriptorPageSize - pageOffset));
					DescriptorPage* page = GetDescriptorPage(*heap, pageIndex, false);
					if (page && page->trackedDescriptorCount.load(std::memory_order_relaxed))
					{
						if (!heap->active.load(std::memory_order_relaxed))
							return;
						for (UINT pageDescriptorIndex = 0;
							pageDescriptorIndex < descriptorsOnPage;
							++pageDescriptorIndex)
						{
							records[outputOffset + copiedDescriptorCount + pageDescriptorIndex] =
								ReadPageDescriptor(*page, pageOffset + pageDescriptorIndex);
						}
					}
					copiedDescriptorCount += descriptorsOnPage;
				}
				return;
			}

			const SIZE_T rangeEnd = DescriptorRangeEnd(start, descriptorCount, descriptorIncrementSize);
			std::shared_lock<std::shared_mutex> lock(gDescriptorMutex);
			for (auto descriptorIt = gDescriptors.lower_bound(start);
				descriptorIt != gDescriptors.end() && descriptorIt->first < rangeEnd;
				++descriptorIt)
			{
				const SIZE_T byteOffset = descriptorIt->first - start;
				if (byteOffset % descriptorIncrementSize != 0)
					continue;
				const SIZE_T descriptorIndex = byteOffset / descriptorIncrementSize;
				if (descriptorIndex < descriptorCount)
					records[outputOffset + descriptorIndex] = descriptorIt->second;
			}
		}

		void WriteDescriptorRange(
			SIZE_T start,
			UINT descriptorCount,
			UINT descriptorIncrementSize,
			DescriptorHeapRecord* heap,
			SIZE_T firstDescriptor,
			const std::vector<DescriptorRecord>& records,
			size_t inputOffset)
		{
			if (heap)
			{
				if (!heap->active.load(std::memory_order_relaxed))
					return;
				UINT writtenDescriptorCount = 0;
				while (writtenDescriptorCount < descriptorCount)
				{
					const SIZE_T descriptorIndex = firstDescriptor + writtenDescriptorCount;
					const size_t pageIndex = static_cast<size_t>(descriptorIndex / DescriptorPageSize);
					const size_t pageOffset = static_cast<size_t>(descriptorIndex % DescriptorPageSize);
					const UINT descriptorsOnPage = (std::min)(
						descriptorCount - writtenDescriptorCount,
						static_cast<UINT>(DescriptorPageSize - pageOffset));
					bool pageNeedsCreation = false;
					for (UINT pageDescriptorIndex = 0;
						pageDescriptorIndex < descriptorsOnPage && !pageNeedsCreation;
						++pageDescriptorIndex)
					{
						pageNeedsCreation = static_cast<bool>(
							records[inputOffset + writtenDescriptorCount + pageDescriptorIndex]);
					}

					DescriptorPage* page = GetDescriptorPage(*heap, pageIndex, pageNeedsCreation);
					if (page)
					{
						if (!heap->active.load(std::memory_order_relaxed))
							return;
						for (UINT pageDescriptorIndex = 0;
							pageDescriptorIndex < descriptorsOnPage;
							++pageDescriptorIndex)
						{
							SetPageDescriptor(
								*heap,
								*page,
								pageOffset + pageDescriptorIndex,
								records[inputOffset + writtenDescriptorCount + pageDescriptorIndex]);
						}
					}
					writtenDescriptorCount += descriptorsOnPage;
				}
				return;
			}

			const SIZE_T rangeEnd = DescriptorRangeEnd(start, descriptorCount, descriptorIncrementSize);
			std::unique_lock<std::shared_mutex> lock(gDescriptorMutex);
			auto destinationIt = gDescriptors.lower_bound(start);
			size_t erasedDescriptorCount = 0;
			while (destinationIt != gDescriptors.end() && destinationIt->first < rangeEnd)
			{
				destinationIt = gDescriptors.erase(destinationIt);
				++erasedDescriptorCount;
			}
			if (erasedDescriptorCount)
				gFallbackTrackedDescriptorCount.fetch_sub(erasedDescriptorCount, std::memory_order_relaxed);
			size_t insertedDescriptorCount = 0;
			for (UINT descriptorIndex = 0; descriptorIndex < descriptorCount; ++descriptorIndex)
			{
				const DescriptorRecord& record = records[inputOffset + descriptorIndex];
				if (!record)
					continue;
				const SIZE_T destination = start +
					static_cast<SIZE_T>(descriptorIndex) * descriptorIncrementSize;
				MarkDescriptorPossiblyTracked(destination);
				gDescriptors[destination] = record;
				++insertedDescriptorCount;
			}
			if (insertedDescriptorCount)
				gFallbackTrackedDescriptorCount.fetch_add(insertedDescriptorCount, std::memory_order_relaxed);
		}

		void CopyHeapDescriptorRange(const DescriptorCopySegment& segment)
		{
			if (!segment.sourceHeap || !segment.destinationHeap || !segment.descriptorCount)
				return;
			if (segment.sourceHeap == segment.destinationHeap &&
				segment.sourceFirstDescriptor == segment.destinationFirstDescriptor)
			{
				return;
			}

			UINT copiedDescriptorCount = 0;
			while (copiedDescriptorCount < segment.descriptorCount)
			{
				const SIZE_T sourceDescriptorIndex =
					segment.sourceFirstDescriptor + copiedDescriptorCount;
				const SIZE_T destinationDescriptorIndex =
					segment.destinationFirstDescriptor + copiedDescriptorCount;
				const size_t sourcePageIndex =
					static_cast<size_t>(sourceDescriptorIndex / DescriptorPageSize);
				const size_t destinationPageIndex =
					static_cast<size_t>(destinationDescriptorIndex / DescriptorPageSize);
				const size_t sourcePageOffset =
					static_cast<size_t>(sourceDescriptorIndex % DescriptorPageSize);
				const size_t destinationPageOffset =
					static_cast<size_t>(destinationDescriptorIndex % DescriptorPageSize);
				const UINT descriptorsOnPages = (std::min)({
					segment.descriptorCount - copiedDescriptorCount,
					static_cast<UINT>(DescriptorPageSize - sourcePageOffset),
					static_cast<UINT>(DescriptorPageSize - destinationPageOffset) });

				DescriptorPage* sourcePage = GetDescriptorPage(
					*segment.sourceHeap,
					sourcePageIndex,
					false);
				const bool sourcePageHasDescriptors = sourcePage &&
					sourcePage->trackedDescriptorCount.load(std::memory_order_relaxed) != 0;
				DescriptorPage* destinationPage = GetDescriptorPage(
					*segment.destinationHeap,
					destinationPageIndex,
					sourcePageHasDescriptors);
				if (!destinationPage)
				{
					copiedDescriptorCount += descriptorsOnPages;
					continue;
				}

				if (!segment.sourceHeap->active.load(std::memory_order_relaxed) ||
					!segment.destinationHeap->active.load(std::memory_order_relaxed))
				{
					return;
				}

				const bool rangesOverlap = sourcePage == destinationPage &&
					sourcePageOffset < destinationPageOffset + descriptorsOnPages &&
					destinationPageOffset < sourcePageOffset + descriptorsOnPages;
				if (rangesOverlap)
				{
					// D3D12 forbids overlapping source and destination ranges. Preserve
					// predictable metadata even for an invalid call that aliases a page.
					thread_local std::vector<DescriptorRecord> samePageDescriptors;
					samePageDescriptors.clear();
					samePageDescriptors.reserve(descriptorsOnPages);
					for (UINT descriptorIndex = 0; descriptorIndex < descriptorsOnPages; ++descriptorIndex)
					{
						samePageDescriptors.push_back(
							ReadPageDescriptor(*sourcePage, sourcePageOffset + descriptorIndex));
					}
					for (UINT descriptorIndex = 0; descriptorIndex < descriptorsOnPages; ++descriptorIndex)
					{
						SetPageDescriptor(
							*segment.destinationHeap,
							*destinationPage,
							destinationPageOffset + descriptorIndex,
							samePageDescriptors[descriptorIndex]);
					}
				}
				else
				{
					for (UINT descriptorIndex = 0; descriptorIndex < descriptorsOnPages; ++descriptorIndex)
					{
						const DescriptorRecord sourceRecord = sourcePageHasDescriptors
							? ReadPageDescriptor(*sourcePage, sourcePageOffset + descriptorIndex)
							: nullptr;
						SetPageDescriptor(
							*segment.destinationHeap,
							*destinationPage,
							destinationPageOffset + descriptorIndex,
							sourceRecord);
					}
				}

				copiedDescriptorCount += descriptorsOnPages;
			}
		}
	}

	void RegisterDescriptorHeap(
		ID3D12DescriptorHeap* descriptorHeap,
		UINT descriptorIncrementSize,
		bool newlyCreated)
	{
		if (!descriptorHeap)
			return;

		const D3D12_DESCRIPTOR_HEAP_DESC description = descriptorHeap->GetDesc();
		if (!description.NumDescriptors || description.Type >= D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES)
			return;

		if (!descriptorIncrementSize)
		{
			ID3D12Device* device = nullptr;
			if (SUCCEEDED(descriptorHeap->GetDevice(IID_PPV_ARGS(&device))) && device)
			{
				descriptorIncrementSize = device->GetDescriptorHandleIncrementSize(description.Type);
				device->Release();
			}
		}
		if (!descriptorIncrementSize)
			return;

		const SIZE_T start = descriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr;
		const SIZE_T end = DescriptorRangeEnd(
			start,
			description.NumDescriptors,
			descriptorIncrementSize);
		if (!start || end <= start)
			return;

		if (!newlyCreated)
		{
			std::shared_lock<std::shared_mutex> lock(gDescriptorHeapMutex);
			const auto existingIt = gDescriptorHeaps.find(start);
			if (existingIt != gDescriptorHeaps.end() && existingIt->second &&
				existingIt->second->identity == descriptorHeap &&
				existingIt->second->end == end &&
				existingIt->second->descriptorIncrementSize == descriptorIncrementSize)
			{
				return;
			}
		}

		auto heap = std::make_shared<DescriptorHeapRecord>();
		heap->identity = descriptorHeap;
		heap->start = start;
		heap->end = end;
		heap->descriptorCount = description.NumDescriptors;
		heap->descriptorIncrementSize = descriptorIncrementSize;
		heap->type = description.Type;
		heap->pageCount =
			(static_cast<size_t>(description.NumDescriptors) + DescriptorPageSize - 1) /
			DescriptorPageSize;
		heap->pages = std::make_unique<std::atomic<DescriptorPage*>[]>(heap->pageCount);
		for (size_t pageIndex = 0; pageIndex < heap->pageCount; ++pageIndex)
			heap->pages[pageIndex].store(nullptr, std::memory_order_relaxed);

		std::unique_lock<std::shared_mutex> lock(gDescriptorHeapMutex);
		const auto duplicateIt = gDescriptorHeaps.find(start);
		if (!newlyCreated && duplicateIt != gDescriptorHeaps.end() && duplicateIt->second &&
			duplicateIt->second->identity == descriptorHeap &&
			duplicateIt->second->end == end &&
			duplicateIt->second->descriptorIncrementSize == descriptorIncrementSize)
		{
			return;
		}

		auto firstOverlappingIt = gDescriptorHeaps.lower_bound(start);
		if (firstOverlappingIt != gDescriptorHeaps.begin())
		{
			auto previousIt = std::prev(firstOverlappingIt);
			if (previousIt->second && previousIt->second->end > start)
				firstOverlappingIt = previousIt;
		}
		while (firstOverlappingIt != gDescriptorHeaps.end() &&
			firstOverlappingIt->first < end)
		{
			if (firstOverlappingIt->second && firstOverlappingIt->second->end > start)
			{
				DeactivateDescriptorHeap(firstOverlappingIt->second);
				gRetiredDescriptorHeaps.push_back(firstOverlappingIt->second);
				firstOverlappingIt = gDescriptorHeaps.erase(firstOverlappingIt);
			}
			else
			{
				++firstOverlappingIt;
			}
		}

		// A heap first observed through SetDescriptorHeaps may predate the heap hook.
		// Move any metadata captured through the compatibility map into its indexed
		// slots. A genuinely new heap instead discards metadata from a recycled handle
		// range so an old resource can never bleed into the new heap.
		{
			std::unique_lock<std::shared_mutex> fallbackLock(gDescriptorMutex);
			auto descriptorIt = gDescriptors.lower_bound(start);
			size_t removedFallbackDescriptorCount = 0;
			while (descriptorIt != gDescriptors.end() && descriptorIt->first < end)
			{
				if (!newlyCreated)
				{
					const SIZE_T byteOffset = descriptorIt->first - start;
					if (byteOffset % descriptorIncrementSize == 0)
					{
						const SIZE_T descriptorIndex = byteOffset / descriptorIncrementSize;
						if (descriptorIndex < description.NumDescriptors)
						{
							SetHeapDescriptor(
								heap.get(),
								start + descriptorIndex * descriptorIncrementSize,
								descriptorIt->second);
						}
					}
				}
				descriptorIt = gDescriptors.erase(descriptorIt);
				++removedFallbackDescriptorCount;
			}
			if (removedFallbackDescriptorCount)
				gFallbackTrackedDescriptorCount.fetch_sub(
					removedFallbackDescriptorCount,
					std::memory_order_relaxed);
		}

		gDescriptorHeaps[start] = std::move(heap);
		gDescriptorHeapGeneration.fetch_add(1, std::memory_order_release);
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

	bool CopyDescriptors(
		UINT destinationRangeCount,
		const D3D12_CPU_DESCRIPTOR_HANDLE* destinationRangeStarts,
		const UINT* destinationRangeSizes,
		UINT sourceRangeCount,
		const D3D12_CPU_DESCRIPTOR_HANDLE* sourceRangeStarts,
		const UINT* sourceRangeSizes,
		UINT descriptorIncrementSize)
	{
		if (!destinationRangeStarts || !sourceRangeStarts || !descriptorIncrementSize)
			return false;

		PerformanceMetrics::ScopedTimer registryTimer(
			PerformanceMetrics::Timing::DescriptorRegistryTrackedPropagation,
			512);
		bool inspectedTrackedDescriptors = false;
		thread_local std::vector<DescriptorRecord> fallbackDescriptors;
		DescriptorCopySegment pendingSegment{};
		const auto propagatePendingSegment = [&]()
		{
			if (!pendingSegment.descriptorCount)
				return;

			ResolveDescriptorCopySegment(pendingSegment, descriptorIncrementSize);
			const bool segmentMayContainTracked = DescriptorRangeMayContainTracked(
				pendingSegment.sourceStart,
				pendingSegment.descriptorCount,
				descriptorIncrementSize,
				pendingSegment.sourceHeap,
				pendingSegment.sourceFirstDescriptor) ||
				DescriptorRangeMayContainTracked(
					pendingSegment.destinationStart,
					pendingSegment.descriptorCount,
					descriptorIncrementSize,
					pendingSegment.destinationHeap,
					pendingSegment.destinationFirstDescriptor);
			if (segmentMayContainTracked)
			{
				inspectedTrackedDescriptors = true;
				if (pendingSegment.sourceHeap && pendingSegment.destinationHeap)
				{
					CopyHeapDescriptorRange(pendingSegment);
				}
				else
				{
					fallbackDescriptors.clear();
					AppendDescriptorRange(
						pendingSegment.sourceStart,
						pendingSegment.descriptorCount,
						descriptorIncrementSize,
						pendingSegment.sourceHeap,
						pendingSegment.sourceFirstDescriptor,
						fallbackDescriptors);
					WriteDescriptorRange(
						pendingSegment.destinationStart,
						pendingSegment.descriptorCount,
						descriptorIncrementSize,
						pendingSegment.destinationHeap,
						pendingSegment.destinationFirstDescriptor,
						fallbackDescriptors,
						0);
				}
			}
			pendingSegment = {};
		};

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
				const SIZE_T destinationStart =
					destinationRangeStarts[destinationRangeIndex].ptr +
					static_cast<SIZE_T>(destinationOffset) * descriptorIncrementSize;
				const SIZE_T sourceStart =
					sourceRangeStarts[sourceRangeIndex].ptr +
					static_cast<SIZE_T>(sourceOffset) * descriptorIncrementSize;
				const bool continuesPendingSegment = pendingSegment.descriptorCount &&
					pendingSegment.descriptorCount <= UINT_MAX - copyCount &&
					pendingSegment.destinationStart +
						static_cast<SIZE_T>(pendingSegment.descriptorCount) * descriptorIncrementSize ==
						destinationStart &&
					pendingSegment.sourceStart +
						static_cast<SIZE_T>(pendingSegment.descriptorCount) * descriptorIncrementSize ==
						sourceStart;
				if (continuesPendingSegment)
					pendingSegment.descriptorCount += copyCount;
				else
				{
					propagatePendingSegment();
					pendingSegment = { destinationStart, sourceStart, copyCount };
				}
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
		propagatePendingSegment();
		return inspectedTrackedDescriptors;
	}

	bool CopyDescriptorsSimple(
		UINT descriptorCount,
		D3D12_CPU_DESCRIPTOR_HANDLE destinationStart,
		D3D12_CPU_DESCRIPTOR_HANDLE sourceStart,
		UINT descriptorIncrementSize)
	{
		if (!descriptorIncrementSize)
			return false;

		DescriptorCopySegment segment{
			destinationStart.ptr,
			sourceStart.ptr,
			descriptorCount };
		ResolveDescriptorCopySegment(segment, descriptorIncrementSize);
		if (!DescriptorRangeMayContainTracked(
				segment.sourceStart,
				segment.descriptorCount,
				descriptorIncrementSize,
				segment.sourceHeap,
				segment.sourceFirstDescriptor) &&
			!DescriptorRangeMayContainTracked(
				segment.destinationStart,
				segment.descriptorCount,
				descriptorIncrementSize,
				segment.destinationHeap,
				segment.destinationFirstDescriptor))
		{
			return false;
		}
		PerformanceMetrics::ScopedTimer registryTimer(
			PerformanceMetrics::Timing::DescriptorRegistryTrackedPropagation,
			512);
		if (segment.sourceHeap && segment.destinationHeap)
		{
			CopyHeapDescriptorRange(segment);
			return true;
		}
		thread_local std::vector<DescriptorRecord> copiedDescriptors;
		copiedDescriptors.clear();
		AppendDescriptorRange(
			segment.sourceStart,
			segment.descriptorCount,
			descriptorIncrementSize,
			segment.sourceHeap,
			segment.sourceFirstDescriptor,
			copiedDescriptors);
		WriteDescriptorRange(
			segment.destinationStart,
			segment.descriptorCount,
			descriptorIncrementSize,
			segment.destinationHeap,
			segment.destinationFirstDescriptor,
			copiedDescriptors,
			0);
		return true;
	}

	bool ResolveDescriptor(
		D3D12_CPU_DESCRIPTOR_HANDLE descriptor,
		RenderPass::ResourceBindingDiagnostic& outBinding)
	{
		const DescriptorRecord record = ReadDescriptorRecord(descriptor.ptr);
		if (!record)
			return false;
		outBinding = *record;
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

	RegistryStatistics GetStatistics()
	{
		RegistryStatistics statistics{};
		statistics.fallbackDescriptorCount =
			gFallbackTrackedDescriptorCount.load(std::memory_order_relaxed);
		{
			std::shared_lock<std::shared_mutex> lock(gDescriptorHeapMutex);
			statistics.descriptorHeapCount = gDescriptorHeaps.size();
			statistics.retiredDescriptorHeapCount = gRetiredDescriptorHeaps.size();
			for (const auto& heapEntry : gDescriptorHeaps)
			{
				const std::shared_ptr<DescriptorHeapRecord>& heap = heapEntry.second;
				if (heap && heap->active.load(std::memory_order_relaxed))
				{
					statistics.heapDescriptorCount +=
						heap->trackedDescriptorCount.load(std::memory_order_relaxed);
				}
			}
		}
		{
			std::lock_guard<std::mutex> lock(gDescriptorMetadataMutex);
			statistics.descriptorMetadataCount = gDescriptorMetadata.size();
		}
		statistics.descriptorCount =
			statistics.heapDescriptorCount + statistics.fallbackDescriptorCount;
		{
			std::shared_lock<std::shared_mutex> lock(gResourceMutex);
			statistics.bufferResourceCount = gResourcesByGpuAddress.size();
		}
		{
			std::shared_lock<std::shared_mutex> lock(gRootSignatureMutex);
			statistics.rootSignatureCount = gRootSignatures.size();
		}
		return statistics;
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
				const DescriptorRecord descriptor = ReadDescriptorRecord(cpuHandle);
				if (descriptor)
				{
					RenderPass::ResourceBindingDiagnostic binding = *descriptor;
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
