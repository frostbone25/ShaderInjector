#include "RenderPass/RenderPassTexturePool.h"
#include "RenderPass/ThreadTextureCacheEntry.h"
#include "RenderPass/ThreadTextureLookup.h"
#include "RenderPass/TexturePool/DefinitionRecord.h"
#include "RenderPass/TexturePool/HistoryBootstrapTexture.h"
#include "RenderPass/TexturePool/InputTextureOverride.h"
#include "RenderPass/TexturePool/QueueFence.h"
#include "RenderPass/TexturePool/RecordedTextureVersion.h"
#include "RenderPass/TexturePool/SubmittedTextures.h"
#include "RenderPass/TexturePool/TextureEntry.h"
#include "RenderPass/TexturePool/TextureVersion.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "IO/ShaderInjectorIO.h"
#include "ShaderResource/ShaderResourceCatalog.h"
#include "StringHelper.h"

namespace RenderPassTexturePool
{
	namespace
	{
		using Microsoft::WRL::ComPtr;

		std::mutex gPoolMutex;
		std::unordered_map<std::string, DefinitionRecord> gDefinitions;
		std::unordered_map<std::string, TextureEntry> gTextures;
		std::unordered_map<ID3D12GraphicsCommandList*, std::unordered_map<ID3D12Resource*, RecordedTextureVersion>> gRecordedTextures;
		std::unordered_map<ID3D12CommandQueue*, QueueFence> gQueueFences;
		std::unordered_map<ID3D12CommandQueue*, std::unordered_map<ID3D12Resource*, RecordedTextureVersion>> gPendingQueueTextures;
		std::atomic<bool> gHasPendingQueueTextures{false};
		std::vector<SubmittedTextures> gSubmittedTextures;
		std::atomic<bool> gHasSubmittedTextures{false};
		std::vector<RecordedTextureVersion> gUnretirableTextures;
		std::atomic<size_t> gRecordedCommandListCount{0};
		using RecordingEpoch = std::shared_ptr<std::atomic<uint64_t>>;
		std::unordered_map<ID3D12GraphicsCommandList*, RecordingEpoch> gRecordingEpochs;
		std::unordered_map<std::string, HistoryBootstrapTexture> gHistoryBootstrapTextures;
		std::atomic<uint64_t> gConfigurationGeneration{1};
		std::atomic<uint64_t> gTextureGeneration{1};
		std::atomic<uint64_t> gFrameIndex{0};
		std::atomic<bool> gHasHistoryResources{false};
		thread_local std::vector<InputTextureOverride> gInputTextureOverrides;
		thread_local size_t gExecutionScopeDepth = 0;
		thread_local uint64_t gExecutionFrameIndex = 0;
		thread_local ID3D12GraphicsCommandList* gExecutionCommandList = nullptr;
		uint64_t CurrentFrameIndex()
		{
			if (gExecutionScopeDepth)
				return gExecutionFrameIndex;
			return gFrameIndex.load(std::memory_order_acquire);
		}
		constexpr D3D12_RESOURCE_STATES shaderReadState =
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

		RecordingEpoch GetRecordingEpochLocked(ID3D12GraphicsCommandList* commandList)
		{
			auto& epoch = gRecordingEpochs[commandList];
			if (!epoch)
				epoch = std::make_shared<std::atomic<uint64_t>>(1);
			return epoch;
		}

		void RecordTextureLocked(ID3D12GraphicsCommandList* commandList, const TextureEntry& entry)
		{
			if (!commandList)
				return;
			GetRecordingEpochLocked(commandList);
			auto [recording, inserted] = gRecordedTextures.try_emplace(commandList);
			if (inserted)
				gRecordedCommandListCount.fetch_add(1, std::memory_order_release);
			for (uint32_t versionIndex = 0; versionIndex < entry.versionCount; ++versionIndex)
			{
				const TextureVersion& version = entry.versions[versionIndex];
				if (!version.resource)
					continue;
				recording->second.try_emplace(version.resource.Get(), RecordedTextureVersion{
																		  version.resource, version.shaderViewHeap, version.renderTargetViewHeap,
																		  entry.allocationBytes / entry.versionCount});
			}
		}

		void PruneSubmittedTexturesLocked()
		{
			gSubmittedTextures.erase(std::remove_if(gSubmittedTextures.begin(), gSubmittedTextures.end(),
													[](const SubmittedTextures& submission)
													{
														return submission.fence->GetCompletedValue() >= submission.fenceValue;
													}),
									 gSubmittedTextures.end());
			gHasSubmittedTextures.store(!gSubmittedTextures.empty(), std::memory_order_release);
		}

		bool TextureDescriptionsEqual(
			const ResolvedTextureDescription& left,
			const ResolvedTextureDescription& right)
		{
			return left.dimension == right.dimension &&
				   left.format == right.format &&
				   left.shaderViewFormat == right.shaderViewFormat &&
				   left.width == right.width &&
				   left.height == right.height &&
				   left.depth == right.depth &&
				   left.arraySize == right.arraySize &&
				   left.mipLevels == right.mipLevels &&
				   left.sampleCount == right.sampleCount &&
				   left.lifetime == right.lifetime &&
				   left.flags == right.flags;
		}

		uint32_t CalculateFullMipCount(uint32_t width, uint32_t height, uint32_t depth)
		{
			uint32_t largestExtent = (std::max)(width, (std::max)(height, depth));
			uint32_t mipLevels = 1;
			while (largestExtent > 1)
			{
				largestExtent >>= 1;
				++mipLevels;
			}
			return mipLevels;
		}

		bool ResolveTextureDescription(
			const ShaderResource::TextureDescriptionDisk& source,
			const ReferenceExtent& referenceExtent,
			ResolvedTextureDescription& outDescription,
			std::string& outError)
		{
			outDescription = {};
			//copy the explicit texture shape first; matching the reference replaces those fields below.
			outDescription.dimension = source.dimension;
			outDescription.format = referenceExtent.fallbackFormat;
			if (source.format)
				outDescription.format = static_cast<DXGI_FORMAT>(source.format);
			outDescription.depth = (std::max)(1u, source.depth);
			outDescription.arraySize = (std::max)(1u, source.arraySize);
			outDescription.sampleCount = (std::max)(1u, source.sampleCount);
			if (source.matchReferenceTexture)
			{
				outDescription.dimension = referenceExtent.dimension;
				outDescription.format = referenceExtent.fallbackFormat;
				outDescription.depth = (std::max)(1u, referenceExtent.depth);
				outDescription.arraySize = (std::max)(1u, referenceExtent.arraySize);
				outDescription.sampleCount = (std::max)(1u, referenceExtent.sampleCount);
			}
			outDescription.shaderViewFormat = outDescription.format;
			if (source.matchReferenceTexture && referenceExtent.fallbackShaderViewFormat != DXGI_FORMAT_UNKNOWN)
				outDescription.shaderViewFormat = referenceExtent.fallbackShaderViewFormat;
			outDescription.lifetime = source.lifetime;

			if (source.matchReferenceTexture)
			{
				outDescription.width = referenceExtent.width;
				outDescription.height = referenceExtent.height;
			}
			else
				switch (source.resolution.mode)
				{
				case ShaderResource::ResolutionMode::Explicit:
					outDescription.width = source.resolution.width;
					outDescription.height = source.resolution.height;
					break;
				case ShaderResource::ResolutionMode::DownscalePowerOfTwo:
				{
					if (!ShaderResource::IsValidDownscaleFactor(source.resolution.downscaleFactor))
					{
						outError = "Runtime texture has an invalid downscale factor.";
						return false;
					}
					const uint32_t factor = source.resolution.downscaleFactor;
					outDescription.width = 0;
					outDescription.height = 0;
					if (referenceExtent.width)
						outDescription.width = (referenceExtent.width + factor - 1) / factor;
					if (referenceExtent.height)
						outDescription.height = (referenceExtent.height + factor - 1) / factor;
					break;
				}
				case ShaderResource::ResolutionMode::Inherit:
				default:
					outDescription.width = referenceExtent.width;
					outDescription.height = referenceExtent.height;
					break;
				}

			if (!outDescription.width || !outDescription.height)
			{
				outError = "Runtime texture resolution could not be resolved from the target pass.";
				return false;
			}
			if (outDescription.format == DXGI_FORMAT_UNKNOWN)
			{
				outError = "Runtime texture format is unknown and no target format is available.";
				return false;
			}
			if (outDescription.dimension == ShaderResource::TextureDimension::Unknown)
			{
				outError = "Runtime texture dimension is unknown.";
				return false;
			}
			if (outDescription.dimension == ShaderResource::TextureDimension::TextureCube)
				outDescription.arraySize = 6;
			else if (outDescription.dimension == ShaderResource::TextureDimension::TextureCubeArray)
			{
				outDescription.arraySize = (std::max)(6u, outDescription.arraySize);
				if (outDescription.arraySize % 6 != 0)
				{
					outError = "Runtime TextureCubeArray slice count must be a multiple of six.";
					return false;
				}
			}
			if (outDescription.dimension == ShaderResource::TextureDimension::Texture3D)
				outDescription.arraySize = 1;

			outDescription.mipLevels = source.mipLevels;
			if (source.matchReferenceTexture)
				outDescription.mipLevels = (std::max)(1u, referenceExtent.mipLevels);
			else if (!source.mipLevels)
			{
				uint32_t mipDepth = 1;
				if (outDescription.dimension == ShaderResource::TextureDimension::Texture3D)
					mipDepth = outDescription.depth;
				outDescription.mipLevels = CalculateFullMipCount(outDescription.width, outDescription.height, mipDepth);
			}
			if (source.allowRenderTarget)
				outDescription.flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
			if (source.allowUnorderedAccess)
				outDescription.flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
			if (outDescription.sampleCount > 1 &&
				(outDescription.mipLevels > 1 || source.allowUnorderedAccess ||
				 outDescription.dimension == ShaderResource::TextureDimension::Texture3D ||
				 outDescription.dimension == ShaderResource::TextureDimension::TextureCube ||
				 outDescription.dimension == ShaderResource::TextureDimension::TextureCubeArray))
			{
				outError = "Multisampled runtime textures cannot have mip levels, UAVs, or a Texture3D layout.";
				return false;
			}
			if (outDescription.depth > (std::numeric_limits<UINT16>::max)() ||
				outDescription.arraySize > (std::numeric_limits<UINT16>::max)() ||
				outDescription.mipLevels > (std::numeric_limits<UINT16>::max)())
			{
				outError = "Runtime texture depth, array size, or mip count exceeds D3D12 limits.";
				return false;
			}
			return true;
		}

		D3D12_RESOURCE_DESC BuildD3D12Description(const ResolvedTextureDescription& description)
		{
			D3D12_RESOURCE_DESC resourceDescription{};
			resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			if (description.dimension == ShaderResource::TextureDimension::Texture3D)
				resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
			resourceDescription.Width = description.width;
			resourceDescription.Height = description.height;
			resourceDescription.DepthOrArraySize = static_cast<UINT16>(description.arraySize);
			if (description.dimension == ShaderResource::TextureDimension::Texture3D)
				resourceDescription.DepthOrArraySize = static_cast<UINT16>(description.depth);
			resourceDescription.MipLevels = static_cast<UINT16>(description.mipLevels);
			resourceDescription.Format = description.format;
			resourceDescription.SampleDesc = {description.sampleCount, 0};
			resourceDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			resourceDescription.Flags = description.flags;
			return resourceDescription;
		}

		void BuildShaderResourceView(
			const ResolvedTextureDescription& description,
			D3D12_SHADER_RESOURCE_VIEW_DESC& view)
		{
			view = {};
			view.Format = description.shaderViewFormat;
			view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			switch (description.dimension)
			{
			case ShaderResource::TextureDimension::Texture2DArray:
				if (description.sampleCount > 1)
				{
					view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
					view.Texture2DMSArray.ArraySize = description.arraySize;
				}
				else
				{
					view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
					view.Texture2DArray.MipLevels = description.mipLevels;
					view.Texture2DArray.ArraySize = description.arraySize;
				}
				break;
			case ShaderResource::TextureDimension::TextureCube:
				view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
				view.TextureCube.MipLevels = description.mipLevels;
				break;
			case ShaderResource::TextureDimension::TextureCubeArray:
				view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
				view.TextureCubeArray.MipLevels = description.mipLevels;
				view.TextureCubeArray.NumCubes = description.arraySize / 6;
				break;
			case ShaderResource::TextureDimension::Texture3D:
				view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
				view.Texture3D.MipLevels = description.mipLevels;
				break;
			case ShaderResource::TextureDimension::Texture2D:
			default:
				if (description.sampleCount > 1)
					view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
				else
				{
					view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
					view.Texture2D.MipLevels = description.mipLevels;
				}
				break;
			}
		}

		void BuildUnorderedAccessView(
			const ResolvedTextureDescription& description,
			D3D12_UNORDERED_ACCESS_VIEW_DESC& view)
		{
			view = {};
			view.Format = description.format;
			switch (description.dimension)
			{
			case ShaderResource::TextureDimension::Texture2DArray:
			case ShaderResource::TextureDimension::TextureCube:
			case ShaderResource::TextureDimension::TextureCubeArray:
				view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
				view.Texture2DArray.ArraySize = description.arraySize;
				break;
			case ShaderResource::TextureDimension::Texture3D:
				view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
				view.Texture3D.WSize = description.depth;
				break;
			case ShaderResource::TextureDimension::Texture2D:
			default:
				view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
				break;
			}
		}

		void BuildRenderTargetView(
			const ResolvedTextureDescription& description,
			D3D12_RENDER_TARGET_VIEW_DESC& view)
		{
			view = {};
			view.Format = description.format;
			switch (description.dimension)
			{
			case ShaderResource::TextureDimension::Texture2DArray:
				if (description.sampleCount > 1)
				{
					view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
					view.Texture2DMSArray.ArraySize = description.arraySize;
					break;
				}
				[[fallthrough]];
			case ShaderResource::TextureDimension::TextureCube:
			case ShaderResource::TextureDimension::TextureCubeArray:
				view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
				view.Texture2DArray.ArraySize = description.arraySize;
				break;
			case ShaderResource::TextureDimension::Texture3D:
				view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
				view.Texture3D.WSize = description.depth;
				break;
			case ShaderResource::TextureDimension::Texture2D:
			default:
				view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
				if (description.sampleCount > 1)
					view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
				break;
			}
		}

		bool CreateTextureVersion(
			ID3D12Device* device,
			const std::string& resourceName,
			const ResolvedTextureDescription& description,
			TextureVersion& outVersion,
			std::string& outError)
		{
			D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport{description.shaderViewFormat};
			D3D12_FORMAT_SUPPORT1 requiredDimensionSupport = D3D12_FORMAT_SUPPORT1_TEXTURE2D;
			if (description.dimension == ShaderResource::TextureDimension::Texture3D)
				requiredDimensionSupport = D3D12_FORMAT_SUPPORT1_TEXTURE3D;
			else if (description.dimension == ShaderResource::TextureDimension::TextureCube || description.dimension == ShaderResource::TextureDimension::TextureCubeArray)
				requiredDimensionSupport = D3D12_FORMAT_SUPPORT1_TEXTURECUBE;
			if (FAILED(device->CheckFeatureSupport(
					D3D12_FEATURE_FORMAT_SUPPORT,
					&formatSupport,
					sizeof(formatSupport))) ||
				(formatSupport.Support1 & requiredDimensionSupport) == 0 ||
				(formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) == 0)
			{
				outError = "Runtime texture format does not support shader sampling.";
				return false;
			}
			if ((description.flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0 &&
				(formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) == 0)
			{
				outError = "Runtime texture format does not support render-target writes.";
				return false;
			}
			if ((description.flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0 &&
				((formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) == 0 ||
				 (formatSupport.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) == 0))
			{
				outError = "Runtime texture format does not support typed UAV writes.";
				return false;
			}

			D3D12_HEAP_PROPERTIES heapProperties{};
			heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
			heapProperties.CreationNodeMask = 1;
			heapProperties.VisibleNodeMask = 1;
			const D3D12_RESOURCE_DESC resourceDescription = BuildD3D12Description(description);
			HRESULT result = device->CreateCommittedResource(
				&heapProperties,
				D3D12_HEAP_FLAG_NONE,
				&resourceDescription,
				shaderReadState,
				nullptr,
				IID_PPV_ARGS(&outVersion.resource));
			if (FAILED(result) || !outVersion.resource)
			{
				outError = "Runtime texture creation failed with " + StringHelper::FormatHRESULT(result);
				return false;
			}

			const std::wstring wideName = StringHelper::Utf8ToWide(resourceName);
			outVersion.resource->SetName(wideName.c_str());
			D3D12_DESCRIPTOR_HEAP_DESC shaderHeapDescription{};
			shaderHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			shaderHeapDescription.NumDescriptors = 1;
			if ((description.flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0)
				shaderHeapDescription.NumDescriptors = 2;
			result = device->CreateDescriptorHeap(
				&shaderHeapDescription,
				IID_PPV_ARGS(&outVersion.shaderViewHeap));
			if (FAILED(result) || !outVersion.shaderViewHeap)
			{
				outError = "Runtime texture descriptor-heap creation failed with " +
						   StringHelper::FormatHRESULT(result);
				return false;
			}

			outVersion.shaderResourceView = outVersion.shaderViewHeap->GetCPUDescriptorHandleForHeapStart();
			D3D12_SHADER_RESOURCE_VIEW_DESC shaderResourceView{};
			BuildShaderResourceView(description, shaderResourceView);
			device->CreateShaderResourceView(
				outVersion.resource.Get(),
				&shaderResourceView,
				outVersion.shaderResourceView);

			if ((description.flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0)
			{
				outVersion.unorderedAccessView = outVersion.shaderResourceView;
				outVersion.unorderedAccessView.ptr += device->GetDescriptorHandleIncrementSize(
					D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				D3D12_UNORDERED_ACCESS_VIEW_DESC unorderedAccessView{};
				BuildUnorderedAccessView(description, unorderedAccessView);
				device->CreateUnorderedAccessView(
					outVersion.resource.Get(),
					nullptr,
					&unorderedAccessView,
					outVersion.unorderedAccessView);
			}

			if ((description.flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0)
			{
				D3D12_DESCRIPTOR_HEAP_DESC renderTargetHeapDescription{};
				renderTargetHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
				renderTargetHeapDescription.NumDescriptors = 1;
				result = device->CreateDescriptorHeap(
					&renderTargetHeapDescription,
					IID_PPV_ARGS(&outVersion.renderTargetViewHeap));
				if (FAILED(result) || !outVersion.renderTargetViewHeap)
				{
					outError = "Runtime RTV descriptor-heap creation failed with " +
							   StringHelper::FormatHRESULT(result);
					return false;
				}
				outVersion.renderTargetView =
					outVersion.renderTargetViewHeap->GetCPUDescriptorHandleForHeapStart();
				D3D12_RENDER_TARGET_VIEW_DESC renderTargetView{};
				BuildRenderTargetView(description, renderTargetView);
				device->CreateRenderTargetView(
					outVersion.resource.Get(),
					&renderTargetView,
					outVersion.renderTargetView);
			}
			return true;
		}

		bool EnsureTextureLocked(
			ID3D12Device* device,
			const DefinitionRecord& definition,
			const ReferenceExtent& referenceExtent,
			std::string& outError)
		{
			ResolvedTextureDescription resolvedDescription{};
			const std::string& resourceId = definition.definition.id;
			auto textureIt = gTextures.find(resourceId);
			TextureEntry newEntry{};
			const std::string& reuseSourceId = definition.definition.reuseFromResourceId;
			if (!reuseSourceId.empty())
			{
				const auto sourceIt = gTextures.find(reuseSourceId);
				if (sourceIt == gTextures.end() || sourceIt->second.device.Get() != device ||
					sourceIt->second.versionCount != 1)
				{
					outError = "The texture to reuse has not been allocated by an earlier pass: " + reuseSourceId;
					return false;
				}
				resolvedDescription = sourceIt->second.description;
				if (textureIt != gTextures.end() && textureIt->second.device.Get() == device &&
					textureIt->second.versions[0].resource.Get() == sourceIt->second.versions[0].resource.Get())
					return true;
				newEntry.versions[0] = sourceIt->second.versions[0];
				newEntry.versionCount = 1;
			}
			else
			{
				if (!ResolveTextureDescription(definition.definition.texture, referenceExtent, resolvedDescription, outError))
					return false;
				if (textureIt != gTextures.end() &&
					textureIt->second.device.Get() == device &&
					textureIt->second.definition.ownerRenderPassId == definition.ownerRenderPassId &&
					TextureDescriptionsEqual(textureIt->second.description, resolvedDescription))
				{
					return true;
				}

				newEntry.versionCount = 1;
				if (resolvedDescription.lifetime == ShaderResource::ResourceLifetime::History)
					newEntry.versionCount = 2;
				for (uint32_t versionIndex = 0; versionIndex < newEntry.versionCount; ++versionIndex)
				{
					std::string resourceName = "Shader Injector Runtime: ";
					if (definition.definition.name.empty())
						resourceName += resourceId;
					else
						resourceName += definition.definition.name;
					if (newEntry.versionCount > 1)
						resourceName += " [" + std::to_string(versionIndex) + "]";
					if (!CreateTextureVersion(device, resourceName, resolvedDescription,
											  newEntry.versions[versionIndex], outError))
						return false;
				}

				const D3D12_RESOURCE_DESC allocationDescription = BuildD3D12Description(resolvedDescription);
				newEntry.allocationBytes = device->GetResourceAllocationInfo(0, 1, &allocationDescription).SizeInBytes *
										   newEntry.versionCount;
			}
			newEntry.device = device;
			newEntry.definition = definition;
			newEntry.description = resolvedDescription;
			newEntry.generation = gTextureGeneration.load(std::memory_order_relaxed) + 1;

			if (textureIt != gTextures.end())
			{
				textureIt->second = std::move(newEntry);
			}
			else
			{
				textureIt = gTextures.emplace(resourceId, std::move(newEntry)).first;
			}

			//Publish invalidation after the replacement is visible under the pool lock.
			gTextureGeneration.store(textureIt->second.generation, std::memory_order_release);
			const TextureEntry& activeEntry = textureIt->second;
			ShaderResource::CatalogEntry catalogEntry{};
			catalogEntry.id = resourceId;
			catalogEntry.name = definition.definition.name;
			if (catalogEntry.name.empty())
				catalogEntry.name = resourceId;
			catalogEntry.origin = ShaderResource::ResourceOrigin::Runtime;
			catalogEntry.lifetime = resolvedDescription.lifetime;
			catalogEntry.ownerRenderPassId = definition.ownerRenderPassId;
			catalogEntry.dimension = resolvedDescription.dimension;
			catalogEntry.width = resolvedDescription.width;
			catalogEntry.height = resolvedDescription.height;
			catalogEntry.depth = resolvedDescription.depth;
			catalogEntry.arraySize = resolvedDescription.arraySize;
			catalogEntry.mipLevels = resolvedDescription.mipLevels;
			catalogEntry.format = static_cast<uint32_t>(resolvedDescription.format);
			catalogEntry.resident = true;
			if (!reuseSourceId.empty())
				catalogEntry.status = "Reusing " + reuseSourceId;
			else if (activeEntry.versionCount > 1)
				catalogEntry.status = "Allocated history pair";
			else
				catalogEntry.status = "Allocated";
			ShaderResourceCatalog::Upsert(catalogEntry);
			std::string reuseDescription;
			if (!reuseSourceId.empty())
				reuseDescription = " reusedFrom=" + reuseSourceId;
			ShaderInjectorIO::WriteToLogFile(
				"RenderPassTexturePool->EnsureTextureLocked: resolved id=" + resourceId +
				" extent=" + std::to_string(resolvedDescription.width) + "x" +
				std::to_string(resolvedDescription.height) +
				" mips=" + std::to_string(resolvedDescription.mipLevels) +
				" versions=" + std::to_string(activeEntry.versionCount) + reuseDescription);
			return true;
		}

		bool FillTextureViewLocked(
			const TextureEntry& entry,
			ShaderResource::TemporalView temporalView,
			TextureView& outTexture)
		{
			outTexture = {};
			if (!entry.versionCount)
				return false;

			uint32_t versionIndex = 0;
			if (entry.versionCount > 1)
			{
				const uint32_t currentIndex = static_cast<uint32_t>(
					CurrentFrameIndex() % entry.versionCount);
				versionIndex = currentIndex;
				if (temporalView == ShaderResource::TemporalView::Previous)
					versionIndex = (currentIndex + entry.versionCount - 1) % entry.versionCount;
			}

			const TextureVersion& version = entry.versions[versionIndex];
			if (temporalView == ShaderResource::TemporalView::Previous &&
				entry.versionCount > 1 && !version.written)
				return false;
			outTexture.resource = version.resource;
			outTexture.shaderViewHeap = version.shaderViewHeap;
			outTexture.renderTargetViewHeap = version.renderTargetViewHeap;
			outTexture.shaderResourceView = version.shaderResourceView;
			outTexture.unorderedAccessView = version.unorderedAccessView;
			outTexture.renderTargetView = version.renderTargetView;
			outTexture.description = entry.description;
			outTexture.initialState = shaderReadState;
			outTexture.generation = entry.generation;
			return outTexture.resource != nullptr;
		}
	} //namespace

	ScopedInputTextureOverrides::ScopedInputTextureOverrides(ID3D12GraphicsCommandList* commandList)
		: previousCount(gInputTextureOverrides.size()), previousCommandList(gExecutionCommandList)
	{
		if (commandList)
			gExecutionCommandList = commandList;
		//Present may advance on another thread while this graph is recorded.
		//Keep every input/output in this execution on the same history pair.
		if (gExecutionScopeDepth++ == 0)
			gExecutionFrameIndex = gFrameIndex.load(std::memory_order_acquire);
	}

	ScopedInputTextureOverrides::~ScopedInputTextureOverrides()
	{
		gInputTextureOverrides.resize(previousCount);
		gExecutionCommandList = previousCommandList;
		--gExecutionScopeDepth;
	}

	void OverrideInputTexture(
		const std::string& resourceId,
		ShaderResource::TemporalView temporalView,
		const TextureView& texture)
	{
		gInputTextureOverrides.push_back({resourceId, temporalView, texture});
	}

	bool GetInputTexture(
		const std::string& resourceId,
		ShaderResource::TemporalView temporalView,
		TextureView& outTexture)
	{
		for (auto overrideIt = gInputTextureOverrides.rbegin(); overrideIt != gInputTextureOverrides.rend(); ++overrideIt)
		{
			if (overrideIt->resourceId == resourceId && overrideIt->temporalView == temporalView)
			{
				outTexture = overrideIt->texture;
				return outTexture.resource != nullptr;
			}
		}
		return GetTexture(resourceId, temporalView, outTexture);
	}

	void PublishConfigurations(const std::vector<RenderPass::RenderPassDisk>& renderPasses)
	{
		std::unordered_map<std::string, DefinitionRecord> definitions;
		bool hasHistoryResources = false;
		for (const RenderPass::RenderPassDisk& renderPass : renderPasses)
		{
			if (!renderPass.enabled)
				continue;
			for (const RenderPass::RuntimeResourceDefinitionDisk& definition : renderPass.runtimeResources)
			{
				if (definition.id.empty())
					continue;
				const auto inserted = definitions.emplace(
					definition.id,
					DefinitionRecord{renderPass.id, definition});
				if (!inserted.second)
				{
					ShaderInjectorIO::WriteToLogFileWarning(
						"RenderPassTexturePool->PublishConfigurations: duplicate runtime resource id=" +
						definition.id);
				}
				hasHistoryResources = hasHistoryResources ||
									  definition.texture.lifetime == ShaderResource::ResourceLifetime::History;
			}
		}
		//The first producer creates the allocation; include write capabilities needed by later aliases.
		for (const auto& [resourceId, record] : definitions)
		{
			if (record.definition.reuseFromResourceId.empty())
				continue;
			std::string sourceId = record.definition.reuseFromResourceId;
			std::unordered_set<std::string> visited{resourceId};
			while (visited.insert(sourceId).second)
			{
				const auto source = definitions.find(sourceId);
				if (source == definitions.end())
					break;
				if (source->second.definition.reuseFromResourceId.empty())
				{
					source->second.definition.texture.allowRenderTarget |= record.definition.texture.allowRenderTarget;
					source->second.definition.texture.allowUnorderedAccess |= record.definition.texture.allowUnorderedAccess;
					break;
				}
				sourceId = source->second.definition.reuseFromResourceId;
			}
		}

		std::lock_guard<std::mutex> lock(gPoolMutex);
		gDefinitions = std::move(definitions);
		gHistoryBootstrapTextures.clear();
		for (auto textureIt = gTextures.begin(); textureIt != gTextures.end();)
		{
			const auto definitionIt = gDefinitions.find(textureIt->first);
			if (definitionIt == gDefinitions.end() ||
				definitionIt->second.ownerRenderPassId != textureIt->second.definition.ownerRenderPassId)
			{
				textureIt = gTextures.erase(textureIt);
			}
			else
			{
				++textureIt;
			}
		}
		gConfigurationGeneration.fetch_add(1, std::memory_order_release);
		gHasHistoryResources.store(hasHistoryResources, std::memory_order_release);
	}

	bool GetHistoryBootstrapTexture(
		ID3D12Device* device,
		const std::string& resourceId,
		TextureView& outTexture)
	{
		outTexture = {};
		if (!device)
			return false;
		std::lock_guard<std::mutex> lock(gPoolMutex);
		const auto definition = gDefinitions.find(resourceId);
		if (definition == gDefinitions.end() ||
			definition->second.definition.texture.lifetime != ShaderResource::ResourceLifetime::History)
			return false;
		auto& bootstrap = gHistoryBootstrapTextures[resourceId];
		if (bootstrap.device.Get() != device || !bootstrap.texture.shaderViewHeap)
		{
			const auto& source = definition->second.definition.texture;
			TextureView texture;
			texture.description.dimension = source.dimension;
			texture.description.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			if (source.format)
				texture.description.format = static_cast<DXGI_FORMAT>(source.format);
			texture.description.shaderViewFormat = texture.description.format;
			texture.description.width = texture.description.height = texture.description.depth = 1;
			texture.description.arraySize = (std::max)(1u, source.arraySize);
			texture.description.mipLevels = 1;
			texture.description.sampleCount = (std::max)(1u, source.sampleCount);
			D3D12_SHADER_RESOURCE_VIEW_DESC view{};
			BuildShaderResourceView(texture.description, view);
			if (view.ViewDimension == D3D12_SRV_DIMENSION_UNKNOWN)
				return false;
			D3D12_DESCRIPTOR_HEAP_DESC heap{};
			heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			heap.NumDescriptors = 1;
			if (FAILED(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&texture.shaderViewHeap))))
				return false;
			texture.shaderResourceView = texture.shaderViewHeap->GetCPUDescriptorHandleForHeapStart();
			device->CreateShaderResourceView(nullptr, &view, texture.shaderResourceView);
			bootstrap = {device, std::move(texture)};
			ShaderInjectorIO::WriteToLogFile(
				"RenderPassTexturePool->GetHistoryBootstrapTexture: zero history for first use id=" + resourceId);
		}
		outTexture = bootstrap.texture;
		return true;
	}

	void MarkPassOutputsWritten(const RenderPass::RenderPassDisk& renderPass)
	{
		const bool ownsHistory = std::any_of(renderPass.runtimeResources.begin(), renderPass.runtimeResources.end(),
											 [](const auto& definition)
											 { return definition.texture.lifetime == ShaderResource::ResourceLifetime::History; });
		if (!ownsHistory)
			return;
		std::lock_guard<std::mutex> lock(gPoolMutex);
		const uint64_t frameIndex = CurrentFrameIndex();
		for (const auto& output : renderPass.outputs)
		{
			if (output.origin != ShaderResource::ResourceOrigin::Runtime ||
				output.temporalView != ShaderResource::TemporalView::Current)
				continue;
			const auto texture = gTextures.find(output.resourceId);
			if (texture != gTextures.end() && texture->second.versionCount > 1 &&
				texture->second.definition.ownerRenderPassId == renderPass.id)
				texture->second.versions[frameIndex % texture->second.versionCount].written = true;
		}
	}

	bool EnsurePassResources(
		const RenderPass::RenderPassDisk& renderPass,
		ID3D12GraphicsCommandList* commandList,
		const ReferenceExtent& referenceExtent,
		std::string& outError)
	{
		outError.clear();
		if (renderPass.runtimeResources.empty())
			return true;
		if (!commandList)
		{
			outError = "Runtime textures require a command list.";
			return false;
		}

		const uint64_t configurationGeneration =
			gConfigurationGeneration.load(std::memory_order_acquire);
		thread_local std::unordered_map<const RenderPass::RenderPassDisk*, ThreadTextureCacheEntry> cache;
		thread_local uint64_t cachedConfigurationGeneration = 0;
		if (cachedConfigurationGeneration != configurationGeneration)
		{
			cache.clear();
			cachedConfigurationGeneration = configurationGeneration;
		}
		const auto cached = cache.find(&renderPass);
		if (cached != cache.end())
		{
			const ThreadTextureCacheEntry& cacheEntry = cached->second;
			if (cacheEntry.renderPass == &renderPass &&
				cacheEntry.commandList == commandList &&
				cacheEntry.configurationGeneration == configurationGeneration &&
				cacheEntry.textureEpoch == gTextureGeneration.load(std::memory_order_acquire) &&
				cacheEntry.recordingEpoch &&
				cacheEntry.recordingValue == cacheEntry.recordingEpoch->load(std::memory_order_acquire) &&
				cacheEntry.referenceExtent.width == referenceExtent.width &&
				cacheEntry.referenceExtent.height == referenceExtent.height &&
				cacheEntry.referenceExtent.dimension == referenceExtent.dimension &&
				cacheEntry.referenceExtent.depth == referenceExtent.depth &&
				cacheEntry.referenceExtent.arraySize == referenceExtent.arraySize &&
				cacheEntry.referenceExtent.mipLevels == referenceExtent.mipLevels &&
				cacheEntry.referenceExtent.sampleCount == referenceExtent.sampleCount &&
				cacheEntry.referenceExtent.fallbackFormat == referenceExtent.fallbackFormat &&
				cacheEntry.referenceExtent.fallbackShaderViewFormat ==
					referenceExtent.fallbackShaderViewFormat)
			{
				outError = cacheEntry.error;
				return cacheEntry.succeeded;
			}
		}

		ComPtr<ID3D12Device> device;
		if (FAILED(commandList->GetDevice(IID_PPV_ARGS(&device))) || !device)
		{
			outError = "Could not query the D3D12 device for runtime textures.";
			return false;
		}

		bool resourcesReady = true;
		uint64_t textureEpoch = 0;
		RecordingEpoch recordingEpoch;
		{
			std::lock_guard<std::mutex> lock(gPoolMutex);
			recordingEpoch = GetRecordingEpochLocked(commandList);
			for (const RenderPass::RuntimeResourceDefinitionDisk& passDefinition : renderPass.runtimeResources)
			{
				const auto definitionIt = gDefinitions.find(passDefinition.id);
				if (definitionIt == gDefinitions.end() ||
					definitionIt->second.ownerRenderPassId != renderPass.id)
				{
					outError = "Runtime texture is not registered to this pass: " + passDefinition.id;
					resourcesReady = false;
					break;
				}
				if (!EnsureTextureLocked(device.Get(), definitionIt->second, referenceExtent, outError))
				{
					resourcesReady = false;
					break;
				}
				RecordTextureLocked(commandList, gTextures.at(passDefinition.id));
			}
			textureEpoch = gTextureGeneration.load(std::memory_order_relaxed);
		}

		//Failed allocations are not permanent: a referenced texture may be produced
		//later in this graph execution. Successful entries cover the whole graph,
		//rather than evicting one another once a chain grows beyond eight passes.
		if (resourcesReady) cache[&renderPass] = {
								&renderPass,
								commandList,
								configurationGeneration,
								textureEpoch,
								recordingEpoch,
								recordingEpoch->load(std::memory_order_relaxed),
								referenceExtent,
								resourcesReady,
								outError};
		return resourcesReady;
	}

	bool GetTexture(
		const std::string& resourceId,
		ShaderResource::TemporalView temporalView,
		TextureView& outTexture)
	{
		outTexture = {};
		thread_local std::unordered_map<std::string, std::array<ThreadTextureLookup, 2>> cache;
		thread_local uint64_t cachedConfigurationGeneration = 0;
		const uint64_t configurationGeneration = gConfigurationGeneration.load(std::memory_order_acquire);
		if (cachedConfigurationGeneration != configurationGeneration)
		{
			cache.clear();
			cachedConfigurationGeneration = configurationGeneration;
		}
		const uint64_t textureEpoch = gTextureGeneration.load(std::memory_order_acquire);
		const uint64_t frameIndex = CurrentFrameIndex();
		size_t temporalIndex = 0;
		if (temporalView == ShaderResource::TemporalView::Previous)
			temporalIndex = 1;
		const auto cached = cache.find(resourceId);
		if (cached != cache.end())
		{
			const ThreadTextureLookup& lookup = cached->second[temporalIndex];
			if (lookup.textureEpoch == textureEpoch &&
				(!gExecutionCommandList || (lookup.recordedCommandList == gExecutionCommandList &&
											lookup.recordingEpoch &&
											lookup.recordingValue == lookup.recordingEpoch->load(std::memory_order_acquire))) &&
				(!lookup.frameSensitive || lookup.frameIndex == frameIndex))
			{
				outTexture = lookup.texture;
				return outTexture.resource != nullptr;
			}
		}

		std::lock_guard<std::mutex> lock(gPoolMutex);
		const auto textureIt = gTextures.find(resourceId);
		if (textureIt == gTextures.end() || !textureIt->second.versionCount)
			return false;
		if (!FillTextureViewLocked(textureIt->second, temporalView, outTexture))
			return false;
		RecordTextureLocked(gExecutionCommandList, textureIt->second);
		RecordingEpoch recordingEpoch;
		if (gExecutionCommandList)
			recordingEpoch = GetRecordingEpochLocked(gExecutionCommandList);
		uint64_t recordingValue = 0;
		if (recordingEpoch)
			recordingValue = recordingEpoch->load(std::memory_order_relaxed);
		cache[resourceId][temporalIndex] = {
			gTextureGeneration.load(std::memory_order_relaxed),
			frameIndex,
			recordingEpoch,
			recordingValue,
			gExecutionCommandList,
			textureIt->second.versionCount > 1,
			outTexture};
		return true;
	}

	bool EnsureUpsampleChainStages(
		const RenderPass::RenderPassDisk& renderPass,
		ID3D12GraphicsCommandList* commandList,
		const TextureView& sourceTexture,
		const TextureView& destinationTexture,
		bool computePipeline,
		std::vector<TextureView>& outStageTargets,
		std::string& outError)
	{
		outStageTargets.clear();
		outError.clear();
		if (!commandList || !sourceTexture.resource || !destinationTexture.resource)
		{
			outError = "Upsample chain requires a command list and valid source/destination textures.";
			return false;
		}
		const ResolvedTextureDescription& source = sourceTexture.description;
		const ResolvedTextureDescription& destination = destinationTexture.description;
		if (sourceTexture.resource.Get() == destinationTexture.resource.Get())
		{
			outError = "Upsample-chain source and destination must be different textures.";
			return false;
		}
		if (source.dimension != ShaderResource::TextureDimension::Texture2D ||
			destination.dimension != ShaderResource::TextureDimension::Texture2D ||
			source.arraySize != 1 || destination.arraySize != 1 ||
			source.sampleCount != 1 || destination.sampleCount != 1)
		{
			outError = "Upsample chains require non-array, non-MSAA Texture2D resources.";
			return false;
		}
		if (source.width > destination.width || source.height > destination.height)
		{
			outError = "Upsample-chain destination must not be smaller than its source.";
			return false;
		}
		bool destinationWritable = (destination.flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0 && destinationTexture.renderTargetView.ptr != 0;
		if (computePipeline)
			destinationWritable = (destination.flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0 && destinationTexture.unorderedAccessView.ptr != 0;
		if (!destinationWritable)
		{
			outError = "Fullscreen upsample-chain destination must allow render-target writes.";
			if (computePipeline)
				outError = "Compute upsample-chain destination must allow unordered-access writes.";
			return false;
		}

		ComPtr<ID3D12Device> device;
		if (FAILED(commandList->GetDevice(IID_PPV_ARGS(&device))) || !device)
		{
			outError = "Could not query the D3D12 device for the upsample chain.";
			return false;
		}

		uint32_t stageWidth = source.width;
		uint32_t stageHeight = source.height;
		std::lock_guard<std::mutex> lock(gPoolMutex);
		for (uint32_t stageIndex = 0; stageIndex < 16; ++stageIndex)
		{
			const uint32_t nextWidth = (std::min)(destination.width, (std::max)(stageWidth + 1, stageWidth * 2));
			const uint32_t nextHeight = (std::min)(destination.height, (std::max)(stageHeight + 1, stageHeight * 2));
			if (nextWidth == destination.width && nextHeight == destination.height)
			{
				outStageTargets.push_back(destinationTexture);
				return true;
			}

			RenderPass::RuntimeResourceDefinitionDisk intermediate{};
			intermediate.id = renderPass.id + ":UpsampleStage:" +
							  std::to_string(nextWidth) + "x" + std::to_string(nextHeight);
			intermediate.name = renderPass.name + " Upsample " +
								std::to_string(nextWidth) + "x" + std::to_string(nextHeight);
			intermediate.texture.dimension = ShaderResource::TextureDimension::Texture2D;
			intermediate.texture.format = static_cast<uint32_t>(destination.format);
			intermediate.texture.resolution.mode = ShaderResource::ResolutionMode::Explicit;
			intermediate.texture.resolution.width = nextWidth;
			intermediate.texture.resolution.height = nextHeight;
			intermediate.texture.mipLevels = 1;
			intermediate.texture.sampleCount = 1;
			intermediate.texture.lifetime = ShaderResource::ResourceLifetime::Transient;
			intermediate.texture.allowRenderTarget = !computePipeline;
			intermediate.texture.allowUnorderedAccess = computePipeline;

			DefinitionRecord definition{renderPass.id, intermediate};
			gDefinitions[intermediate.id] = definition;
			ReferenceExtent explicitExtent{};
			explicitExtent.dimension = ShaderResource::TextureDimension::Texture2D;
			explicitExtent.width = nextWidth;
			explicitExtent.height = nextHeight;
			explicitExtent.fallbackFormat = destination.format;
			explicitExtent.fallbackShaderViewFormat = destination.shaderViewFormat;
			if (!EnsureTextureLocked(device.Get(), definition, explicitExtent, outError))
				return false;
			const auto textureIt = gTextures.find(intermediate.id);
			TextureView target;
			if (textureIt == gTextures.end() ||
				!FillTextureViewLocked(textureIt->second, ShaderResource::TemporalView::Current, target))
			{
				outError = "Could not resolve an allocated upsample-chain stage.";
				return false;
			}
			RecordTextureLocked(commandList, textureIt->second);
			outStageTargets.push_back(std::move(target));
			stageWidth = nextWidth;
			stageHeight = nextHeight;
		}

		outError = "Upsample chain exceeded the maximum supported stage count.";
		return false;
	}

	void AdvanceFrame()
	{
		if (gHasPendingQueueTextures.load(std::memory_order_acquire) ||
			gHasSubmittedTextures.load(std::memory_order_acquire))
		{
			std::lock_guard<std::mutex> lock(gPoolMutex);
			PruneSubmittedTexturesLocked();
			for (auto& [commandQueue, pendingVersions] : gPendingQueueTextures)
			{
				if (pendingVersions.empty())
					continue;
				SubmittedTextures submission{};
				submission.versions.reserve(pendingVersions.size());
				for (const auto& version : pendingVersions)
					submission.versions.push_back(version.second);
				QueueFence& queueFence = gQueueFences.at(commandQueue);
				if (!queueFence.fence)
				{
					ComPtr<ID3D12Device> device;
					if (FAILED(commandQueue->GetDevice(IID_PPV_ARGS(&device))) || !device ||
						FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&queueFence.fence))))
					{
						gUnretirableTextures.insert(gUnretirableTextures.end(), submission.versions.begin(), submission.versions.end());
						ShaderInjectorIO::WriteToLogFileError("RenderPassTexturePool: could not create a texture-retirement fence.");
						continue;
					}
				}
				submission.fence = queueFence.fence;
				submission.fenceValue = ++queueFence.nextValue;
				const HRESULT result = commandQueue->Signal(submission.fence.Get(), submission.fenceValue);
				if (FAILED(result))
				{
					gUnretirableTextures.insert(gUnretirableTextures.end(), submission.versions.begin(), submission.versions.end());
					ShaderInjectorIO::WriteToLogFileError("RenderPassTexturePool: texture-retirement signal failed with " + StringHelper::FormatHRESULT(result));
					continue;
				}
				gSubmittedTextures.push_back(std::move(submission));
				gHasSubmittedTextures.store(true, std::memory_order_release);
			}
			gPendingQueueTextures.clear();
			gHasPendingQueueTextures.store(false, std::memory_order_release);
		}
		if (gHasHistoryResources.load(std::memory_order_relaxed))
			gFrameIndex.fetch_add(1, std::memory_order_release);
	}

	bool HasRecordedCommandListWork()
	{
		return gRecordedCommandListCount.load(std::memory_order_acquire) != 0;
	}

	void ResetCommandListRecording(ID3D12GraphicsCommandList* commandList)
	{
		std::lock_guard<std::mutex> lock(gPoolMutex);
		if (gRecordedTextures.erase(commandList))
			gRecordedCommandListCount.fetch_sub(1, std::memory_order_release);
		const auto epoch = gRecordingEpochs.find(commandList);
		if (epoch != gRecordingEpochs.end())
			epoch->second->fetch_add(1, std::memory_order_release);
		PruneSubmittedTexturesLocked();
	}

	void NotifyCommandListsSubmitted(ID3D12CommandQueue* commandQueue, UINT commandListCount, ID3D12CommandList* const* commandLists)
	{
		if (!HasRecordedCommandListWork() || !commandQueue || !commandLists || !commandListCount)
			return;
		const D3D12_COMMAND_LIST_TYPE queueType = commandQueue->GetDesc().Type;
		if (queueType != D3D12_COMMAND_LIST_TYPE_DIRECT && queueType != D3D12_COMMAND_LIST_TYPE_COMPUTE)
			return;

		std::lock_guard<std::mutex> lock(gPoolMutex);
		bool recordedAnyTexture = false;
		for (UINT index = 0; index < commandListCount; ++index)
		{
			const auto recorded = gRecordedTextures.find(reinterpret_cast<ID3D12GraphicsCommandList*>(commandLists[index]));
			if (recorded == gRecordedTextures.end())
				continue;
			recordedAnyTexture = true;
			auto& pendingVersions = gPendingQueueTextures[commandQueue];
			for (const auto& version : recorded->second)
				pendingVersions.try_emplace(version.first, version.second);
		}
		if (recordedAnyTexture)
		{
			QueueFence& queueFence = gQueueFences[commandQueue];
			if (!queueFence.queue)
				queueFence.queue = commandQueue;
			gHasPendingQueueTextures.store(true, std::memory_order_release);
		}
	}

	void ReleaseResources()
	{
		std::lock_guard<std::mutex> lock(gPoolMutex);
		gDefinitions.clear();
		gHistoryBootstrapTextures.clear();
		gTextures.clear();
		gRecordedTextures.clear();
		gPendingQueueTextures.clear();
		gHasPendingQueueTextures.store(false, std::memory_order_release);
		gSubmittedTextures.clear();
		gHasSubmittedTextures.store(false, std::memory_order_release);
		gUnretirableTextures.clear();
		gQueueFences.clear();
		gRecordingEpochs.clear();
		gRecordedCommandListCount.store(0, std::memory_order_release);
		gFrameIndex.store(0, std::memory_order_release);
		gHasHistoryResources.store(false, std::memory_order_release);
		gConfigurationGeneration.fetch_add(1, std::memory_order_release);
	}

	void LogPerformanceStatistics()
	{
		size_t liveTextures = 0, retiredTextures = 0;
		uint64_t liveBytes = 0, retiredBytes = 0;
		{
			std::lock_guard<std::mutex> lock(gPoolMutex);
			PruneSubmittedTexturesLocked();
			liveTextures = gTextures.size();
			std::unordered_set<ID3D12Resource*> activeResources;
			for (const auto& texture : gTextures)
			{
				liveBytes += texture.second.allocationBytes;
				for (uint32_t versionIndex = 0; versionIndex < texture.second.versionCount; ++versionIndex)
					activeResources.insert(texture.second.versions[versionIndex].resource.Get());
			}
			std::unordered_set<ID3D12Resource*> retiredResources;
			const auto countRetired = [&](const RecordedTextureVersion& version)
			{
				if (version.resource && activeResources.find(version.resource.Get()) == activeResources.end() &&
					retiredResources.insert(version.resource.Get()).second)
					retiredBytes += version.allocationBytes;
			};
			for (const auto& recording : gRecordedTextures)
				for (const auto& version : recording.second)
					countRetired(version.second);
			for (const auto& pendingQueue : gPendingQueueTextures)
				for (const auto& version : pendingQueue.second)
					countRetired(version.second);
			for (const auto& batch : gSubmittedTextures)
				for (const auto& version : batch.versions)
					countRetired(version);
			for (const auto& version : gUnretirableTextures)
				countRetired(version);
			retiredTextures = retiredResources.size();
		}
		ShaderInjectorIO::WriteToLogFile(StringHelper::Format(
			"RenderPassTexturePool->Performance: liveTextures=%zu liveBytes=%llu retiredTextures=%zu retiredBytes=%llu",
			liveTextures, static_cast<unsigned long long>(liveBytes),
			retiredTextures, static_cast<unsigned long long>(retiredBytes)));
	}
} //namespace RenderPassTexturePool
