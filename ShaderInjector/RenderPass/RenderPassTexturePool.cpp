#include "RenderPass/RenderPassTexturePool.h"

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

		struct DefinitionRecord
		{
			std::string ownerRenderPassId;
			RenderPass::RuntimeResourceDefinitionDisk definition;
		};

		struct TextureVersion
		{
			ComPtr<ID3D12Resource> resource;
			ComPtr<ID3D12DescriptorHeap> shaderViewHeap;
			ComPtr<ID3D12DescriptorHeap> renderTargetViewHeap;
			D3D12_CPU_DESCRIPTOR_HANDLE shaderResourceView{};
			D3D12_CPU_DESCRIPTOR_HANDLE unorderedAccessView{};
			D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView{};
		};

		struct TextureEntry
		{
			ComPtr<ID3D12Device> device;
			DefinitionRecord definition;
			ResolvedTextureDescription description;
			std::array<TextureVersion, 2> versions;
			uint32_t versionCount = 0;
			uint64_t generation = 0;
		};

		std::mutex gPoolMutex;
		std::unordered_map<std::string, DefinitionRecord> gDefinitions;
		std::unordered_map<std::string, TextureEntry> gTextures;
		// Configuration and resolution changes are rare. Retaining replaced allocations
		// avoids releasing a texture while an already-recorded game command list uses it.
		std::vector<TextureEntry> gRetiredTextures;
		std::atomic<uint64_t> gConfigurationGeneration{ 1 };
		std::atomic<uint64_t> gTextureGeneration{ 1 };
		std::atomic<uint64_t> gFrameIndex{ 0 };
		std::atomic<bool> gHasHistoryResources{ false };
		struct InputTextureOverride
		{
			std::string resourceId;
			ShaderResource::TemporalView temporalView;
			TextureView texture;
		};
		thread_local std::vector<InputTextureOverride> gInputTextureOverrides;
		constexpr D3D12_RESOURCE_STATES shaderReadState =
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

		bool TextureDescriptionsEqual(
			const ResolvedTextureDescription& left,
			const ResolvedTextureDescription& right)
		{
			return left.dimension == right.dimension &&
				left.format == right.format &&
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
			outDescription.dimension = source.matchReferenceTexture
				? referenceExtent.dimension
				: source.dimension;
			outDescription.format = source.matchReferenceTexture
				? referenceExtent.fallbackFormat
				: (source.format
					? static_cast<DXGI_FORMAT>(source.format)
					: referenceExtent.fallbackFormat);
			outDescription.depth = source.matchReferenceTexture
				? (std::max)(1u, referenceExtent.depth)
				: (std::max)(1u, source.depth);
			outDescription.arraySize = source.matchReferenceTexture
				? (std::max)(1u, referenceExtent.arraySize)
				: (std::max)(1u, source.arraySize);
			outDescription.sampleCount = source.matchReferenceTexture
				? (std::max)(1u, referenceExtent.sampleCount)
				: (std::max)(1u, source.sampleCount);
			outDescription.lifetime = source.lifetime;

			if (source.matchReferenceTexture)
			{
				outDescription.width = referenceExtent.width;
				outDescription.height = referenceExtent.height;
			}
			else switch (source.resolution.mode)
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
					outDescription.width = referenceExtent.width
						? (referenceExtent.width + factor - 1) / factor
						: 0;
					outDescription.height = referenceExtent.height
						? (referenceExtent.height + factor - 1) / factor
						: 0;
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

			outDescription.mipLevels = source.matchReferenceTexture
				? (std::max)(1u, referenceExtent.mipLevels)
				: (source.mipLevels
					? source.mipLevels
					: CalculateFullMipCount(
					outDescription.width,
					outDescription.height,
					outDescription.dimension == ShaderResource::TextureDimension::Texture3D
						? outDescription.depth
						: 1u));
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
			resourceDescription.Dimension = description.dimension == ShaderResource::TextureDimension::Texture3D
				? D3D12_RESOURCE_DIMENSION_TEXTURE3D
				: D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			resourceDescription.Width = description.width;
			resourceDescription.Height = description.height;
			resourceDescription.DepthOrArraySize = static_cast<UINT16>(
				description.dimension == ShaderResource::TextureDimension::Texture3D
					? description.depth
					: description.arraySize);
			resourceDescription.MipLevels = static_cast<UINT16>(description.mipLevels);
			resourceDescription.Format = description.format;
			resourceDescription.SampleDesc = { description.sampleCount, 0 };
			resourceDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			resourceDescription.Flags = description.flags;
			return resourceDescription;
		}

		void BuildShaderResourceView(
			const ResolvedTextureDescription& description,
			D3D12_SHADER_RESOURCE_VIEW_DESC& view)
		{
			view = {};
			view.Format = description.format;
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
					view.ViewDimension = description.sampleCount > 1
						? D3D12_RTV_DIMENSION_TEXTURE2DMS
						: D3D12_RTV_DIMENSION_TEXTURE2D;
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
			D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport{ description.format };
			const D3D12_FORMAT_SUPPORT1 requiredDimensionSupport =
				description.dimension == ShaderResource::TextureDimension::Texture3D
					? D3D12_FORMAT_SUPPORT1_TEXTURE3D
					: (description.dimension == ShaderResource::TextureDimension::TextureCube ||
						description.dimension == ShaderResource::TextureDimension::TextureCubeArray
							? D3D12_FORMAT_SUPPORT1_TEXTURECUBE
							: D3D12_FORMAT_SUPPORT1_TEXTURE2D);
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
			shaderHeapDescription.NumDescriptors =
				(description.flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0 ? 2u : 1u;
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
			if (!ResolveTextureDescription(
				definition.definition.texture,
				referenceExtent,
				resolvedDescription,
				outError))
			{
				return false;
			}

			const std::string& resourceId = definition.definition.id;
			auto textureIt = gTextures.find(resourceId);
			if (textureIt != gTextures.end() &&
				textureIt->second.device.Get() == device &&
				textureIt->second.definition.ownerRenderPassId == definition.ownerRenderPassId &&
				TextureDescriptionsEqual(textureIt->second.description, resolvedDescription))
			{
				return true;
			}

			TextureEntry newEntry{};
			newEntry.device = device;
			newEntry.definition = definition;
			newEntry.description = resolvedDescription;
			newEntry.versionCount = resolvedDescription.lifetime == ShaderResource::ResourceLifetime::History
				? 2u
				: 1u;
			newEntry.generation = gTextureGeneration.fetch_add(1, std::memory_order_relaxed);
			for (uint32_t versionIndex = 0; versionIndex < newEntry.versionCount; ++versionIndex)
			{
				const std::string resourceName = "Shader Injector Runtime: " +
					(definition.definition.name.empty() ? resourceId : definition.definition.name) +
					(newEntry.versionCount > 1 ? " [" + std::to_string(versionIndex) + "]" : "");
				if (!CreateTextureVersion(
					device,
					resourceName,
					resolvedDescription,
					newEntry.versions[versionIndex],
					outError))
				{
					return false;
				}
			}

			if (textureIt != gTextures.end())
			{
				gRetiredTextures.push_back(std::move(textureIt->second));
				textureIt->second = std::move(newEntry);
			}
			else
			{
				textureIt = gTextures.emplace(resourceId, std::move(newEntry)).first;
			}

			const TextureEntry& activeEntry = textureIt->second;
			ShaderResource::CatalogEntry catalogEntry{};
			catalogEntry.id = resourceId;
			catalogEntry.name = definition.definition.name.empty()
				? resourceId
				: definition.definition.name;
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
			catalogEntry.status = activeEntry.versionCount > 1
				? "Allocated history pair"
				: "Allocated";
			ShaderResourceCatalog::Upsert(catalogEntry);
			ShaderInjectorIO::WriteToLogFile(
				"RenderPassTexturePool->EnsureTextureLocked: allocated id=" + resourceId +
				" extent=" + std::to_string(resolvedDescription.width) + "x" +
				std::to_string(resolvedDescription.height) +
				" mips=" + std::to_string(resolvedDescription.mipLevels) +
				" versions=" + std::to_string(activeEntry.versionCount));
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
					gFrameIndex.load(std::memory_order_acquire) % entry.versionCount);
				versionIndex = temporalView == ShaderResource::TemporalView::Previous
					? (currentIndex + entry.versionCount - 1) % entry.versionCount
					: currentIndex;
			}

			const TextureVersion& version = entry.versions[versionIndex];
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
	}

	ScopedInputTextureOverrides::ScopedInputTextureOverrides()
		: previousCount(gInputTextureOverrides.size())
	{
	}

	ScopedInputTextureOverrides::~ScopedInputTextureOverrides()
	{
		gInputTextureOverrides.resize(previousCount);
	}

	void OverrideInputTexture(
		const std::string& resourceId,
		ShaderResource::TemporalView temporalView,
		const TextureView& texture)
	{
		gInputTextureOverrides.push_back({ resourceId, temporalView, texture });
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
			for (const RenderPass::RuntimeResourceDefinitionDisk& definition : renderPass.runtimeResources)
			{
				if (definition.id.empty())
					continue;
				const auto inserted = definitions.emplace(
					definition.id,
					DefinitionRecord{ renderPass.id, definition });
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

		std::lock_guard<std::mutex> lock(gPoolMutex);
		gDefinitions = std::move(definitions);
		for (auto textureIt = gTextures.begin(); textureIt != gTextures.end();)
		{
			const auto definitionIt = gDefinitions.find(textureIt->first);
			if (definitionIt == gDefinitions.end() ||
				definitionIt->second.ownerRenderPassId != textureIt->second.definition.ownerRenderPassId)
			{
				gRetiredTextures.push_back(std::move(textureIt->second));
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
		struct ThreadCacheEntry
		{
			const RenderPass::RenderPassDisk* renderPass = nullptr;
			ID3D12GraphicsCommandList* commandList = nullptr;
			uint64_t configurationGeneration = 0;
			ReferenceExtent referenceExtent;
			bool succeeded = false;
			std::string error;
		};
		thread_local std::array<ThreadCacheEntry, 8> cache{};
		thread_local size_t nextCacheEntry = 0;
		for (const ThreadCacheEntry& cacheEntry : cache)
		{
			if (cacheEntry.renderPass == &renderPass &&
				cacheEntry.commandList == commandList &&
				cacheEntry.configurationGeneration == configurationGeneration &&
				cacheEntry.referenceExtent.width == referenceExtent.width &&
				cacheEntry.referenceExtent.height == referenceExtent.height &&
				cacheEntry.referenceExtent.dimension == referenceExtent.dimension &&
				cacheEntry.referenceExtent.depth == referenceExtent.depth &&
				cacheEntry.referenceExtent.arraySize == referenceExtent.arraySize &&
				cacheEntry.referenceExtent.mipLevels == referenceExtent.mipLevels &&
				cacheEntry.referenceExtent.sampleCount == referenceExtent.sampleCount &&
				cacheEntry.referenceExtent.fallbackFormat == referenceExtent.fallbackFormat)
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
		{
			std::lock_guard<std::mutex> lock(gPoolMutex);
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
			}
		}

		cache[nextCacheEntry] = {
			&renderPass,
			commandList,
			configurationGeneration,
			referenceExtent,
			resourcesReady,
			outError };
		nextCacheEntry = (nextCacheEntry + 1) % cache.size();
		return resourcesReady;
	}

	bool GetTexture(
		const std::string& resourceId,
		ShaderResource::TemporalView temporalView,
		TextureView& outTexture)
	{
		outTexture = {};
		struct ThreadTextureLookup
		{
			std::string resourceId;
			ShaderResource::TemporalView temporalView = ShaderResource::TemporalView::Current;
			uint64_t configurationGeneration = 0;
			uint64_t textureEpoch = 0;
			uint64_t frameIndex = 0;
			bool frameSensitive = false;
			TextureView texture;
		};
		thread_local std::array<ThreadTextureLookup, 16> cache{};
		thread_local size_t nextCacheEntry = 0;
		const uint64_t configurationGeneration = gConfigurationGeneration.load(std::memory_order_acquire);
		const uint64_t textureEpoch = gTextureGeneration.load(std::memory_order_acquire);
		const uint64_t frameIndex = gFrameIndex.load(std::memory_order_acquire);
		for (const ThreadTextureLookup& lookup : cache)
		{
			if (lookup.resourceId == resourceId &&
				lookup.temporalView == temporalView &&
				lookup.configurationGeneration == configurationGeneration &&
				lookup.textureEpoch == textureEpoch &&
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
		cache[nextCacheEntry] = {
			resourceId,
			temporalView,
			configurationGeneration,
			textureEpoch,
			frameIndex,
			textureIt->second.versionCount > 1,
			outTexture };
		nextCacheEntry = (nextCacheEntry + 1) % cache.size();
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
		const bool destinationWritable = computePipeline
			? (destination.flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0 &&
				destinationTexture.unorderedAccessView.ptr != 0
			: (destination.flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0 &&
				destinationTexture.renderTargetView.ptr != 0;
		if (!destinationWritable)
		{
			outError = computePipeline
				? "Compute upsample-chain destination must allow unordered-access writes."
				: "Fullscreen upsample-chain destination must allow render-target writes.";
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

			DefinitionRecord definition{ renderPass.id, intermediate };
			gDefinitions[intermediate.id] = definition;
			ReferenceExtent explicitExtent{};
			explicitExtent.dimension = ShaderResource::TextureDimension::Texture2D;
			explicitExtent.width = nextWidth;
			explicitExtent.height = nextHeight;
			explicitExtent.fallbackFormat = destination.format;
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
			outStageTargets.push_back(std::move(target));
			stageWidth = nextWidth;
			stageHeight = nextHeight;
		}

		outError = "Upsample chain exceeded the maximum supported stage count.";
		return false;
	}

	void AdvanceFrame()
	{
		if (gHasHistoryResources.load(std::memory_order_relaxed))
			gFrameIndex.fetch_add(1, std::memory_order_release);
	}

	void ReleaseResources()
	{
		std::lock_guard<std::mutex> lock(gPoolMutex);
		gDefinitions.clear();
		gTextures.clear();
		gRetiredTextures.clear();
		gFrameIndex.store(0, std::memory_order_release);
		gHasHistoryResources.store(false, std::memory_order_release);
		gConfigurationGeneration.fetch_add(1, std::memory_order_release);
	}
}
