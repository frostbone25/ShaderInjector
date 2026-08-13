#include "ShaderResource/ShaderResourceRuntime.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <wrl/client.h>

#include "HookD3D12/HookD3D12RenderPass.h"
#include "Performance/PerformanceMetrics.h"
#include "RenderPass/RenderPassResourceRegistry.h"
#include "ShaderResource/DatabaseShaderResources.h"
#include "StringHelper.h"

using Microsoft::WRL::ComPtr;

namespace ShaderResourceRuntime
{
	namespace
	{
		constexpr uint32_t ddsMagic = 0x20534444;
		constexpr uint32_t fourCcFlag = 0x4;
		constexpr uint32_t rgbFlag = 0x40;
		constexpr uint32_t FourCc(char a, char b, char c, char d)
		{
			return static_cast<uint8_t>(a) | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
				(static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
				(static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
		}
		constexpr uint32_t fourCcDx10 = FourCc('D', 'X', '1', '0');
		constexpr uint32_t fourCcDxt1 = FourCc('D', 'X', 'T', '1');
		constexpr uint32_t fourCcDxt3 = FourCc('D', 'X', 'T', '3');
		constexpr uint32_t fourCcDxt5 = FourCc('D', 'X', 'T', '5');
		constexpr uint32_t fourCcAti1 = FourCc('A', 'T', 'I', '1');
		constexpr uint32_t fourCcAti2 = FourCc('A', 'T', 'I', '2');

#pragma pack(push, 1)
		struct DdsPixelFormat
		{
			uint32_t size, flags, fourCC, rgbBitCount, rMask, gMask, bMask, aMask;
		};
		struct DdsHeader
		{
			uint32_t size, flags, height, width, pitchOrLinearSize, depth, mipMapCount;
			uint32_t reserved1[11];
			DdsPixelFormat pixelFormat;
			uint32_t caps, caps2, caps3, caps4, reserved2;
		};
		struct DdsHeaderDx10
		{
			DXGI_FORMAT format;
			uint32_t resourceDimension, miscFlag, arraySize, miscFlags2;
		};
#pragma pack(pop)

		struct DdsImage
		{
			DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
			uint32_t width = 0, height = 0, mipLevels = 1, arraySize = 1;
			bool cube = false;
			std::vector<uint8_t> pixels;
		};

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

		struct CommandListSlot
		{
			ComPtr<ID3D12Device> device;
			ComPtr<ID3D12DescriptorHeap> heap;
			UINT capacity = 0;
			UINT descriptorIncrementSize = 0;
			ID3D12RootSignature* cachedRootSignature = nullptr;
			uint32_t cachedMaximumTrackedDescriptors = 0;
			std::vector<RenderPassResourceRegistry::DescriptorTableLayout> layouts;
			std::vector<ActiveTable> activeTables;
			std::unordered_map<uint64_t, RenderPassResourceRegistry::DescriptorBindingLocation> bindingLocations;
			std::unordered_map<std::string, TextureGpu*> resolvedTextures;
			std::vector<ID3D12DescriptorHeap*> restoreHeaps;
			std::vector<RootTableRestore> restoreRootTables;
			bool computePipeline = false;
			bool pendingRestore = false;
		};

		std::mutex gTextureMutex;
		std::unordered_map<ID3D12Device*, std::unordered_map<std::string, std::unique_ptr<TextureGpu>>> gTextures;
		std::mutex gCommandListSlotMutex;
		std::unordered_map<ID3D12GraphicsCommandList*, std::unique_ptr<CommandListSlot>> gCommandListSlots;
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

		DXGI_FORMAT LegacyFormat(const DdsPixelFormat& format)
		{
			if (format.flags & fourCcFlag)
			{
				switch (format.fourCC)
				{
					case fourCcDxt1: return DXGI_FORMAT_BC1_UNORM;
					case fourCcDxt3: return DXGI_FORMAT_BC2_UNORM;
					case fourCcDxt5: return DXGI_FORMAT_BC3_UNORM;
					case fourCcAti1: return DXGI_FORMAT_BC4_UNORM;
					case fourCcAti2: return DXGI_FORMAT_BC5_UNORM;
				}
			}
			if ((format.flags & rgbFlag) && format.rgbBitCount == 32)
			{
				if (format.rMask == 0x000000ff && format.gMask == 0x0000ff00 && format.bMask == 0x00ff0000)
					return DXGI_FORMAT_R8G8B8A8_UNORM;
				if (format.rMask == 0x00ff0000 && format.gMask == 0x0000ff00 && format.bMask == 0x000000ff)
					return DXGI_FORMAT_B8G8R8A8_UNORM;
			}
			return DXGI_FORMAT_UNKNOWN;
		}

		bool LoadDds(const std::string& path, DdsImage& outImage, std::string& outError)
		{
			std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
			if (!file)
			{
				outError = "Could not open DDS file: " + path;
				return false;
			}
			uint32_t magic = 0;
			DdsHeader header{};
			file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
			file.read(reinterpret_cast<char*>(&header), sizeof(header));
			if (!file || magic != ddsMagic || header.size != 124 || header.pixelFormat.size != 32)
			{
				outError = "DDS header is invalid: " + path;
				return false;
			}

			outImage = {};
			outImage.width = header.width;
			outImage.height = header.height;
			outImage.mipLevels = (std::max)(1u, header.mipMapCount);
			if (header.pixelFormat.fourCC == fourCcDx10)
			{
				DdsHeaderDx10 dx10{};
				file.read(reinterpret_cast<char*>(&dx10), sizeof(dx10));
				if (!file || dx10.resourceDimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || !dx10.arraySize)
				{
					outError = "Only Texture2D DDS resources are supported.";
					return false;
				}
				outImage.format = dx10.format;
				outImage.arraySize = dx10.arraySize;
				outImage.cube = (dx10.miscFlag & 0x4u) != 0;
			}
			else
			{
				outImage.format = LegacyFormat(header.pixelFormat);
				outImage.arraySize = (header.caps2 & 0x00000200u) ? 6u : 1u;
				outImage.cube = outImage.arraySize == 6;
			}
			if (!outImage.width || !outImage.height || outImage.format == DXGI_FORMAT_UNKNOWN)
			{
				outError = "DDS format or dimensions are unsupported.";
				return false;
			}
			outImage.pixels.assign(std::istreambuf_iterator<char>(file), {});
			if (outImage.pixels.empty())
			{
				outError = "DDS contains no image data.";
				return false;
			}
			return true;
		}

		void SurfaceInfo(DXGI_FORMAT format, UINT width, UINT height, UINT64& rowBytes, UINT& rows, UINT64& bytes)
		{
			UINT blockBytes = 0;
			switch (format)
			{
				case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB:
				case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM: blockBytes = 8; break;
				case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB:
				case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB:
				case DXGI_FORMAT_BC5_UNORM: case DXGI_FORMAT_BC5_SNORM:
				case DXGI_FORMAT_BC6H_UF16: case DXGI_FORMAT_BC6H_SF16:
				case DXGI_FORMAT_BC7_UNORM: case DXGI_FORMAT_BC7_UNORM_SRGB: blockBytes = 16; break;
			}
			if (blockBytes)
			{
				const UINT blocksWide = (std::max)(1u, (width + 3) / 4);
				rows = (std::max)(1u, (height + 3) / 4);
				rowBytes = static_cast<UINT64>(blocksWide) * blockBytes;
				bytes = rowBytes * rows;
				return;
			}
			UINT bitsPerPixel = 32;
			switch (format)
			{
				case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_A8_UNORM: bitsPerPixel = 8; break;
				case DXGI_FORMAT_R8G8_UNORM: case DXGI_FORMAT_R16_FLOAT: bitsPerPixel = 16; break;
				case DXGI_FORMAT_R32G32_FLOAT:
				case DXGI_FORMAT_R32G32_UINT:
				case DXGI_FORMAT_R32G32_SINT:
				case DXGI_FORMAT_R16G16B16A16_FLOAT:
				case DXGI_FORMAT_R16G16B16A16_UNORM:
				case DXGI_FORMAT_R16G16B16A16_UINT:
				case DXGI_FORMAT_R16G16B16A16_SNORM:
				case DXGI_FORMAT_R16G16B16A16_SINT: bitsPerPixel = 64; break;
				case DXGI_FORMAT_R32G32B32_FLOAT:
				case DXGI_FORMAT_R32G32B32_UINT:
				case DXGI_FORMAT_R32G32B32_SINT: bitsPerPixel = 96; break;
				case DXGI_FORMAT_R32G32B32A32_FLOAT:
				case DXGI_FORMAT_R32G32B32A32_UINT:
				case DXGI_FORMAT_R32G32B32A32_SINT: bitsPerPixel = 128; break;
			}
			rowBytes = (static_cast<UINT64>(width) * bitsPerPixel + 7) / 8;
			rows = height;
			bytes = rowBytes * rows;
		}

		TextureGpu* GetOrCreateTexture(
			ID3D12Device* device,
			ID3D12GraphicsCommandList* commandList,
			const ShaderResource::TextureDisk& disk,
			std::string& outError)
		{
			std::lock_guard<std::mutex> lock(gTextureMutex);
			auto& cachedEntry = gTextures[device][disk.id];
			if (!cachedEntry)
				cachedEntry = std::make_unique<TextureGpu>();
			TextureGpu& cached = *cachedEntry;
			if (cached.texture)
				return &cached;

			DdsImage image{};
			if (!LoadDds(disk.filePath, image, outError))
				return nullptr;
			D3D12_RESOURCE_DESC description{};
			description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			description.Width = image.width;
			description.Height = image.height;
			description.DepthOrArraySize = static_cast<UINT16>(image.arraySize);
			description.MipLevels = static_cast<UINT16>(image.mipLevels);
			description.Format = image.format;
			description.SampleDesc.Count = 1;
			description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		D3D12_HEAP_PROPERTIES defaultHeap{ D3D12_HEAP_TYPE_DEFAULT };
		HRESULT result = device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &description,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&cached.texture));
		if (FAILED(result))
		{
			outError = "DDS texture creation failed with " + StringHelper::FormatHRESULT(result);
			return nullptr;
		}

		const UINT subresourceCount = image.arraySize * image.mipLevels;
		std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(subresourceCount);
		std::vector<UINT> rowCounts(subresourceCount);
		std::vector<UINT64> rowSizes(subresourceCount);
		UINT64 uploadSize = 0;
		device->GetCopyableFootprints(&description, 0, subresourceCount, 0, layouts.data(), rowCounts.data(), rowSizes.data(), &uploadSize);
		D3D12_RESOURCE_DESC uploadDescription{};
		uploadDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		uploadDescription.Width = uploadSize;
		uploadDescription.Height = 1;
		uploadDescription.DepthOrArraySize = 1;
		uploadDescription.MipLevels = 1;
		uploadDescription.SampleDesc.Count = 1;
		uploadDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		D3D12_HEAP_PROPERTIES uploadHeap{ D3D12_HEAP_TYPE_UPLOAD };
		result = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDescription,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&cached.upload));
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
		for (UINT arrayIndex = 0, subresource = 0; arrayIndex < image.arraySize; ++arrayIndex)
		{
			UINT width = image.width, height = image.height;
			for (UINT mip = 0; mip < image.mipLevels; ++mip, ++subresource)
			{
				UINT64 sourceRowBytes = 0, sourceBytes = 0;
				UINT sourceRows = 0;
				SurfaceInfo(image.format, width, height, sourceRowBytes, sourceRows, sourceBytes);
				if (sourceOffset + sourceBytes > image.pixels.size())
				{
					cached.upload->Unmap(0, nullptr);
					outError = "DDS subresource data is truncated.";
					cached = {};
					return nullptr;
				}
				for (UINT row = 0; row < sourceRows; ++row)
					memcpy(mapped + layouts[subresource].Offset + static_cast<SIZE_T>(row) * layouts[subresource].Footprint.RowPitch,
						image.pixels.data() + sourceOffset + static_cast<SIZE_T>(row) * sourceRowBytes,
						static_cast<size_t>(sourceRowBytes));
				sourceOffset += static_cast<size_t>(sourceBytes);
				width = (std::max)(1u, width >> 1);
				height = (std::max)(1u, height >> 1);
			}
		}
		cached.upload->Unmap(0, nullptr);
		for (UINT subresource = 0; subresource < subresourceCount; ++subresource)
		{
			D3D12_TEXTURE_COPY_LOCATION destination{ cached.texture.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX };
			destination.SubresourceIndex = subresource;
			D3D12_TEXTURE_COPY_LOCATION source{ cached.upload.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT };
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
		cached.view.Format = image.format;
		cached.view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			if (image.cube)
			{
				cached.view.ViewDimension = image.arraySize > 6 ? D3D12_SRV_DIMENSION_TEXTURECUBEARRAY : D3D12_SRV_DIMENSION_TEXTURECUBE;
				if (image.arraySize > 6)
				{
					cached.view.TextureCubeArray.MipLevels = image.mipLevels;
					cached.view.TextureCubeArray.NumCubes = image.arraySize / 6;
				}
				else
					cached.view.TextureCube.MipLevels = image.mipLevels;
		}
		else if (image.arraySize > 1)
		{
			cached.view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			cached.view.Texture2DArray.MipLevels = image.mipLevels;
			cached.view.Texture2DArray.ArraySize = image.arraySize;
		}
		else
		{
			cached.view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			cached.view.Texture2D.MipLevels = image.mipLevels;
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
				if (slot.computePipeline)
					commandList->SetComputeRootDescriptorTable(binding.rootParameterIndex, binding.descriptorHandle);
				else
					commandList->SetGraphicsRootDescriptorTable(binding.rootParameterIndex, binding.descriptorHandle);
			}
		}
	}

	bool BindResources(
		const RenderPass::RenderPassDisk& renderPass,
		ID3D12GraphicsCommandList* commandList,
		const RenderPassMipChain::GraphicsStateSnapshot& gameState,
		bool computePipeline,
		std::string& outError)
	{
		outError.clear();
		if (renderPass.shaderResources.empty())
			return true;
		PerformanceMetrics::Increment(PerformanceMetrics::Counter::ShaderResourceBindAttempted);
		PerformanceMetrics::ScopedTimer bindTimer(PerformanceMetrics::Timing::BindShaderResources);
		if (!commandList || !gameState.rootSignature)
		{
			outError = "Shader resources require a captured root signature.";
			return false;
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
			slot.bindingLocations.clear();
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
		UINT totalDescriptors = 0;
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
			const UINT64 requiredBytes = static_cast<UINT64>(layoutIt->descriptorCount) * sourceHeap->descriptorIncrementSize;
			const UINT64 heapBytes = static_cast<UINT64>(sourceHeap->descriptorCount) * sourceHeap->descriptorIncrementSize;
			if (byteOffset + requiredBytes > heapBytes)
				continue;
			ActiveTable table{};
			table.rootParameterIndex = binding.rootParameterIndex;
			table.heapType = layoutIt->heapType;
			table.descriptorCount = layoutIt->descriptorCount;
			table.originalGpu = { binding.value };
			table.originalCpu = { sourceHeap->cpuStart.ptr + byteOffset };
			if (table.heapType == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
			{
				table.customOffset = totalDescriptors;
				totalDescriptors += table.descriptorCount;
			}
			slot.activeTables.push_back(table);
		}
		if (!totalDescriptors)
		{
			outError = "No bounded CBV/SRV/UAV descriptor table is active.";
			return false;
		}

		if (!slot.heap || slot.capacity < totalDescriptors)
		{
			D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
			heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			heapDescription.NumDescriptors = totalDescriptors;
			heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			slot.heap.Reset();
			const HRESULT result = device->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&slot.heap));
			if (FAILED(result))
			{
				outError = "Shader-resource descriptor heap creation failed with " + StringHelper::FormatHRESULT(result);
				return false;
			}
			slot.capacity = totalDescriptors;
		}
		if (!slot.descriptorIncrementSize)
			slot.descriptorIncrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		const UINT increment = slot.descriptorIncrementSize;
		const D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = slot.heap->GetCPUDescriptorHandleForHeapStart();
		const D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = slot.heap->GetGPUDescriptorHandleForHeapStart();
		for (const ActiveTable& table : slot.activeTables)
		{
			if (table.heapType == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
				device->CopyDescriptorsSimple(table.descriptorCount,
					{ cpuStart.ptr + static_cast<SIZE_T>(table.customOffset) * increment }, table.originalCpu,
					D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}

		for (const RenderPass::ShaderResourceReferenceDisk& reference : renderPass.shaderResources)
		{
			const ShaderResource::TextureDisk* disk = DatabaseShaderResources::FindShaderResourceById(reference.resourceId);
			if (!disk)
			{
				outError = "Shader resource is missing: " + reference.resourceId;
				return false;
			}
			const uint64_t bindingKey =
				(static_cast<uint64_t>(reference.registerSpace) << 32) | reference.shaderRegister;
			auto locationIt = slot.bindingLocations.find(bindingKey);
			if (locationIt == slot.bindingLocations.end())
			{
				RenderPassResourceRegistry::DescriptorBindingLocation location{};
				if (!RenderPassResourceRegistry::FindDescriptorBinding(gameState.rootSignature,
					D3D12_DESCRIPTOR_RANGE_TYPE_SRV, reference.shaderRegister, reference.registerSpace,
					renderPass.maximumTrackedDescriptors, location))
				{
					outError = "No SRV root binding exists for t" + std::to_string(reference.shaderRegister) +
						", space" + std::to_string(reference.registerSpace) + ".";
					return false;
				}
				locationIt = slot.bindingLocations.emplace(bindingKey, location).first;
			}
			const auto& location = locationIt->second;
			const auto tableIt = std::find_if(slot.activeTables.begin(), slot.activeTables.end(), [&](const auto& table)
			{
				return table.rootParameterIndex == location.rootParameterIndex;
			});
			if (tableIt == slot.activeTables.end() || location.tableOffset >= tableIt->descriptorCount)
			{
				outError = "The configured SRV table is not active on this draw or dispatch.";
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
			const UINT descriptorOffset = tableIt->customOffset + location.tableOffset;
			device->CopyDescriptorsSimple(
				1,
				{ cpuStart.ptr + static_cast<SIZE_T>(descriptorOffset) * increment },
				texture->srvHeap->GetCPUDescriptorHandleForHeapStart(),
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		}

		ID3D12DescriptorHeap* samplerHeap = nullptr;
		for (const auto& heap : gameState.descriptorHeaps)
			if (heap.type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) samplerHeap = heap.heap;
		ID3D12DescriptorHeap* heaps[] = { slot.heap.Get(), samplerHeap };
		commandList->SetDescriptorHeaps(samplerHeap ? 2u : 1u, heaps);
		for (const ActiveTable& table : slot.activeTables)
		{
			const D3D12_GPU_DESCRIPTOR_HANDLE handle = table.heapType == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
				? D3D12_GPU_DESCRIPTOR_HANDLE{ gpuStart.ptr + static_cast<UINT64>(table.customOffset) * increment }
				: table.originalGpu;
			if (computePipeline)
				commandList->SetComputeRootDescriptorTable(table.rootParameterIndex, handle);
			else
				commandList->SetGraphicsRootDescriptorTable(table.rootParameterIndex, handle);
		}
		slot.restoreHeaps.clear();
		for (const auto& heap : gameState.descriptorHeaps)
		{
			if (heap.heap)
				slot.restoreHeaps.push_back(heap.heap);
		}
		slot.restoreRootTables.clear();
		for (const auto& binding : gameState.rootBindings)
		{
			if (binding.type == RenderPassMipChain::RootArgumentType::DescriptorTable)
				slot.restoreRootTables.push_back({
					binding.rootParameterIndex,
					{ binding.value } });
		}
		slot.computePipeline = computePipeline;
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

	void ResetCommandList(ID3D12GraphicsCommandList* commandList)
	{
		if (CommandListSlot* slot = FindCommandListSlot(commandList))
			slot->pendingRestore = false;
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
		}
		gCachedCommandList = nullptr;
		gCachedCommandListSlot = nullptr;
	}
}
