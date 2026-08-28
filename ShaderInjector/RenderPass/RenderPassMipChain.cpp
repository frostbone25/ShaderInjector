#include "RenderPass/RenderPassMipChain.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include <wrl/client.h>

#include "HookD3D12RenderPass.h"
#include "Performance/PerformanceMetrics.h"
#include "RenderPass/RenderPassResourceRegistry.h"
#include "IO/ShaderInjectorIO.h"
#include "ShaderResource/ShaderResourceCatalog.h"
#include "StringHelper.h"

namespace RenderPassMipChain
{
	using Microsoft::WRL::ComPtr;

	namespace
	{
		struct DeviceResources
		{
			DeviceResources()
			{
				for (std::atomic<int8_t>& support : graphicsMipGeneratorFormatSupport)
					support.store(-1, std::memory_order_relaxed);
				for (std::atomic<int8_t>& support : computeMipGeneratorFormatSupport)
					support.store(-1, std::memory_order_relaxed);
			}

			ComPtr<ID3D12Device> device;
			ComPtr<ID3D12RootSignature> rootSignature;
			std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> pipelines;
			std::unordered_map<std::string, std::string> pipelineErrors;
			std::array<std::atomic<int8_t>, 256> graphicsMipGeneratorFormatSupport;
			std::array<std::atomic<int8_t>, 256> computeMipGeneratorFormatSupport;
			UINT shaderResourceDescriptorIncrement = 0;
			UINT renderTargetDescriptorIncrement = 0;
		};

		struct MipTextureResources
		{
			ComPtr<ID3D12Resource> texture;
			ComPtr<ID3D12DescriptorHeap> sourceHeap;
			ComPtr<ID3D12DescriptorHeap> renderTargetHeap;
			ComPtr<ID3D12DescriptorHeap> fullMipChainHeap;
			UINT width = 0;
			UINT height = 0;
			UINT mipLevelCount = 0;
			DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
			UINT shader4ComponentMapping = 0;
			D3D12_CPU_DESCRIPTOR_HANDLE fullMipChainDescriptor{};
			bool fullMipChainDescriptorInitialized = false;
			bool allSubresourcesShaderReadable = false;
			bool computePipeline = false;
			std::wstring eventName;
		};

		struct ExecutionSlot
		{
			struct RecordedTextureState
			{
				MipTextureResources* resources = nullptr;
				bool allSubresourcesShaderReadable = false;
			};

			// A slot can execute different pass groups over its lifetime. Keep each
			// pass's resources under a stable identity so changing execution order
			// never turns into texture and descriptor-heap recreation.
			std::unordered_map<std::string, MipTextureResources> mipTexturesByRenderPassId;
			std::vector<RecordedTextureState> recordedTextureStates;
			ComPtr<ID3D12DescriptorHeap> nativeDescriptorHeap;
			UINT nativeDescriptorCapacity = 0;
			ID3D12RootSignature* cachedLayoutRootSignature = nullptr;
			UINT cachedLayoutMaximumDescriptors = 0;
			std::vector<RenderPassResourceRegistry::DescriptorTableLayout> cachedDescriptorTableLayouts;
			ComPtr<ID3D12Fence> retirementFence;
			UINT64 retirementFenceValue = 0;
			bool recorded = false;
			bool submitted = false;
			bool retirementBlocked = false;
		};

		struct CommandListPool
		{
			ComPtr<ID3D12Device> device;
			std::vector<std::unique_ptr<ExecutionSlot>> slots;
			std::vector<ExecutionSlot*> recordedSlots;
		};

		struct QueueFence
		{
			ComPtr<ID3D12Fence> fence;
			UINT64 nextValue = 0;
		};

		struct ResolvedMipPass
		{
			const RenderPass::RenderPassDisk* renderPass = nullptr;
			size_t resultIndex = 0;
			RenderPassResourceRegistry::DescriptorBindingLocation bindingLocation;
			D3D12_CPU_DESCRIPTOR_HANDLE sourceDescriptor{};
			RenderPass::ResourceBindingDiagnostic sourceMetadata;
			ComPtr<ID3D12Resource> sourceResource;
			MipTextureResources* resources = nullptr;
		};

		struct ActiveDescriptorTable
		{
			UINT rootParameterIndex = UINT32_MAX;
			D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
			UINT descriptorCount = 0;
			D3D12_GPU_DESCRIPTOR_HANDLE originalGpuHandle{};
			D3D12_CPU_DESCRIPTOR_HANDLE originalCpuHandle{};
			UINT customHeapOffset = 0;
		};

		std::mutex gDeviceResourcesMutex;
		std::unordered_map<ID3D12Device*, std::unique_ptr<DeviceResources>> gDeviceResources;

		std::mutex gExecutionPoolMutex;
		std::unordered_map<ID3D12GraphicsCommandList*, CommandListPool> gCommandListPools;
		std::unordered_map<ID3D12CommandQueue*, QueueFence> gQueueFences;
		std::atomic<uint32_t> gRecordedCommandListCount = 0;

		struct ThreadDeviceResourcesLookup
		{
			ID3D12Device* device = nullptr;
			DeviceResources* resources = nullptr;
		};

		struct ThreadGeneratorPipelineLookup
		{
			DeviceResources* deviceResources = nullptr;
			const RenderPass::RenderPassDisk* renderPass = nullptr;
			DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
			bool computePipeline = false;
			ID3D12PipelineState* pipelineState = nullptr;
		};

		struct ThreadMipBindingLookup
		{
			const RenderPass::RenderPassDisk* renderPass = nullptr;
			ID3D12RootSignature* rootSignature = nullptr;
			bool computePipeline = false;
			RenderPassResourceRegistry::DescriptorBindingLocation location;
			bool found = false;
		};

		thread_local ThreadDeviceResourcesLookup gThreadDeviceResourcesLookup;
		thread_local std::array<ThreadGeneratorPipelineLookup, 16> gThreadGeneratorPipelineLookups;
		thread_local size_t gNextThreadGeneratorPipelineLookup = 0;
		thread_local std::array<ThreadMipBindingLookup, 16> gThreadMipBindingLookups;
		thread_local size_t gNextThreadMipBindingLookup = 0;

		UINT CalculateMipLevelCount(UINT width, UINT height)
		{
			UINT levelCount = 1;
			for (UINT dimension = (std::max)(width, height); dimension > 1; dimension >>= 1)
				++levelCount;
			return levelCount;
		}

		const DescriptorHeapBinding* FindHeapForGpuHandle(
			const GraphicsStateSnapshot& gameState,
			D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle,
			D3D12_DESCRIPTOR_HEAP_TYPE expectedType)
		{
			for (const DescriptorHeapBinding& heap : gameState.descriptorHeaps)
			{
				if (heap.type != expectedType || !heap.gpuStart.ptr || !heap.descriptorIncrementSize ||
					!heap.descriptorCount)
				{
					continue;
				}

				const UINT64 heapByteSize = static_cast<UINT64>(heap.descriptorIncrementSize) * heap.descriptorCount;
				if (gpuHandle.ptr >= heap.gpuStart.ptr && gpuHandle.ptr < heap.gpuStart.ptr + heapByteSize)
					return &heap;
			}
			return nullptr;
		}

		const RootArgumentSnapshot* FindRootBinding(
			const GraphicsStateSnapshot& gameState,
			UINT rootParameterIndex,
			RootArgumentType bindingType)
		{
			for (const RootArgumentSnapshot& binding : gameState.rootBindings)
			{
				if (binding.rootParameterIndex == rootParameterIndex && binding.type == bindingType)
					return &binding;
			}
			return nullptr;
		}

		bool FindMipBinding(
			const RenderPass::RenderPassDisk& renderPass,
			ID3D12RootSignature* rootSignature,
			bool computePipeline,
			RenderPassResourceRegistry::DescriptorBindingLocation& outLocation)
		{
			for (const ThreadMipBindingLookup& lookup : gThreadMipBindingLookups)
			{
				if (lookup.renderPass == &renderPass && lookup.rootSignature == rootSignature &&
					lookup.computePipeline == computePipeline)
				{
					outLocation = lookup.location;
					return lookup.found;
				}
			}

			ThreadMipBindingLookup lookup{};
			lookup.renderPass = &renderPass;
			lookup.rootSignature = rootSignature;
			lookup.computePipeline = computePipeline;
			const D3D12_SHADER_VISIBILITY shaderVisibility = computePipeline
				? D3D12_SHADER_VISIBILITY_ALL
				: D3D12_SHADER_VISIBILITY_PIXEL;
			lookup.found = RenderPassResourceRegistry::FindDescriptorBinding(
				rootSignature,
				D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				renderPass.sourceTextureShaderRegister,
				renderPass.sourceTextureRegisterSpace,
				renderPass.maximumTrackedDescriptors,
				shaderVisibility,
				lookup.location) ||
				RenderPassResourceRegistry::FindUniqueDescriptorBindingByShaderRegister(
					rootSignature,
					D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
					renderPass.sourceTextureShaderRegister,
					renderPass.maximumTrackedDescriptors,
					shaderVisibility,
					lookup.location);
			outLocation = lookup.location;
			gThreadMipBindingLookups[gNextThreadMipBindingLookup] = lookup;
			gNextThreadMipBindingLookup =
				(gNextThreadMipBindingLookup + 1) % gThreadMipBindingLookups.size();
			return lookup.found;
		}

		bool ResolveMipPass(
			const RenderPass::RenderPassDisk& renderPass,
			const GraphicsStateSnapshot& gameState,
			bool computePipeline,
			ResolvedMipPass& outPass,
			std::string& outError)
		{
			PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Timing::ResolveMipPass);
			outError.clear();
			if (!RenderPass::HasCompiledShaders(renderPass))
			{
				outError = "Mip-chain shaders have not been compiled and loaded.";
				return false;
			}

			if (!FindMipBinding(renderPass, gameState.rootSignature, computePipeline, outPass.bindingLocation))
			{
				outError = StringHelper::Format(
					"The target root signature does not expose an unambiguous t%u binding (configured space%u).",
					renderPass.sourceTextureShaderRegister,
					renderPass.sourceTextureRegisterSpace);
				return false;
			}
			if (outPass.bindingLocation.tableContainsUnboundedRange)
			{
				outError = "Mip-chain replacement does not yet support an unbounded descriptor table.";
				return false;
			}

			const RootArgumentSnapshot* tableBinding = FindRootBinding(
				gameState,
				outPass.bindingLocation.rootParameterIndex,
				RootArgumentType::DescriptorTable);
			if (!tableBinding || !tableBinding->value)
			{
				outError = std::string("The source texture's ") +
					(computePipeline ? "compute" : "graphics") + " descriptor table is not currently bound.";
				return false;
			}

			const DescriptorHeapBinding* heap = FindHeapForGpuHandle(
				gameState,
				{ tableBinding->value },
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			if (!heap)
			{
				outError = "The source texture's shader-visible descriptor heap is unavailable.";
				return false;
			}

			const UINT64 tableByteOffset = tableBinding->value - heap->gpuStart.ptr;
			const UINT64 sourceByteOffset = tableByteOffset +
				static_cast<UINT64>(outPass.bindingLocation.tableOffset) * heap->descriptorIncrementSize;
			const UINT64 heapByteSize = static_cast<UINT64>(heap->descriptorCount) * heap->descriptorIncrementSize;
			if (sourceByteOffset >= heapByteSize)
			{
				outError = "The selected source texture register falls outside the bound descriptor heap.";
				return false;
			}

			outPass.sourceDescriptor.ptr = heap->cpuStart.ptr + sourceByteOffset;
			if (!RenderPassResourceRegistry::ResolveDescriptor(outPass.sourceDescriptor, outPass.sourceMetadata) ||
				outPass.sourceMetadata.bindingType != "SRV" || !outPass.sourceMetadata.resourcePointer)
			{
				outError = StringHelper::Format(
					"No live Texture2D SRV metadata is available for t%u, space%u.",
					renderPass.sourceTextureShaderRegister,
					renderPass.sourceTextureRegisterSpace);
				return false;
			}
			outPass.sourceResource = reinterpret_cast<ID3D12Resource*>(outPass.sourceMetadata.resourcePointer);
			if (outPass.sourceMetadata.resourceDimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
				(outPass.sourceMetadata.descriptorViewDimension != UINT32_MAX &&
					outPass.sourceMetadata.descriptorViewDimension != D3D12_SRV_DIMENSION_TEXTURE2D) ||
				outPass.sourceMetadata.resourceDepthOrArraySize != 1 ||
				outPass.sourceMetadata.resourceSampleCount != 1)
			{
				outError = "The selected mip source must be a non-array, non-MSAA Texture2D SRV.";
				return false;
			}
			if (outPass.sourceMetadata.descriptorPlaneSlice != 0)
			{
				outError = "The selected mip source uses a non-zero texture plane, which is not a color mip source.";
				return false;
			}
			return true;
		}

		bool CreateGeneratorRootSignature(DeviceResources& resources, std::string& outError)
		{
			if (resources.rootSignature)
				return true;

			D3D12_DESCRIPTOR_RANGE sourceRange{};
			sourceRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			sourceRange.NumDescriptors = 1;
			sourceRange.BaseShaderRegister = 0;
			sourceRange.RegisterSpace = 0;
			sourceRange.OffsetInDescriptorsFromTableStart = 0;
			D3D12_DESCRIPTOR_RANGE destinationRange{};
			destinationRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
			destinationRange.NumDescriptors = 1;
			destinationRange.BaseShaderRegister = 0;
			destinationRange.RegisterSpace = 0;
			destinationRange.OffsetInDescriptorsFromTableStart = 0;

			D3D12_ROOT_PARAMETER parameters[3]{};
			parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameters[0].DescriptorTable.NumDescriptorRanges = 1;
			parameters[0].DescriptorTable.pDescriptorRanges = &sourceRange;
			parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			parameters[1].Constants.ShaderRegister = 0;
			parameters[1].Constants.RegisterSpace = 0;
			parameters[1].Constants.Num32BitValues = 4;
			parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			parameters[2].DescriptorTable.NumDescriptorRanges = 1;
			parameters[2].DescriptorTable.pDescriptorRanges = &destinationRange;
			parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

			D3D12_STATIC_SAMPLER_DESC sampler{};
			sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			sampler.MipLODBias = 0.0f;
			sampler.MaxAnisotropy = 1;
			sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
			sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
			sampler.MinLOD = 0.0f;
			sampler.MaxLOD = D3D12_FLOAT32_MAX;
			sampler.ShaderRegister = 0;
			sampler.RegisterSpace = 0;
			sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

			D3D12_ROOT_SIGNATURE_DESC description{};
			description.NumParameters = static_cast<UINT>(std::size(parameters));
			description.pParameters = parameters;
			description.NumStaticSamplers = 1;
			description.pStaticSamplers = &sampler;
			description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

			ComPtr<ID3DBlob> serialized;
			ComPtr<ID3DBlob> errors;
			HRESULT result = D3D12SerializeRootSignature(
				&description,
				D3D_ROOT_SIGNATURE_VERSION_1,
				&serialized,
				&errors);
			if (FAILED(result) || !serialized)
			{
				outError = "Mip-chain root-signature serialization failed with " + StringHelper::FormatHRESULT(result);
				if (errors && errors->GetBufferPointer())
					outError += ": " + std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
				return false;
			}

			result = resources.device->CreateRootSignature(
				0,
				serialized->GetBufferPointer(),
				serialized->GetBufferSize(),
				IID_PPV_ARGS(&resources.rootSignature));
			if (FAILED(result) || !resources.rootSignature)
			{
				outError = "Mip-chain root-signature creation failed with " + StringHelper::FormatHRESULT(result);
				return false;
			}
			resources.rootSignature->SetName(L"Shader Injector Mip Chain Root Signature");
			return true;
		}

		DeviceResources* GetDeviceResources(ID3D12Device* device, std::string& outError)
		{
			if (gThreadDeviceResourcesLookup.device == device &&
				gThreadDeviceResourcesLookup.resources)
			{
				return gThreadDeviceResourcesLookup.resources;
			}

			std::lock_guard<std::mutex> lock(gDeviceResourcesMutex);
			auto& resources = gDeviceResources[device];
			if (!resources)
			{
				resources = std::make_unique<DeviceResources>();
				resources->device = device;
				resources->shaderResourceDescriptorIncrement =
					device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				resources->renderTargetDescriptorIncrement =
					device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			}
			if (!CreateGeneratorRootSignature(*resources, outError))
				return nullptr;
			gThreadDeviceResourcesLookup = { device, resources.get() };
			return resources.get();
		}

		ID3D12PipelineState* GetGeneratorPipeline(
			DeviceResources& deviceResources,
			const RenderPass::RenderPassDisk& renderPass,
			DXGI_FORMAT format,
			bool computePipeline,
			std::string& outError)
		{
			for (const ThreadGeneratorPipelineLookup& lookup : gThreadGeneratorPipelineLookups)
			{
				if (lookup.deviceResources == &deviceResources &&
					lookup.renderPass == &renderPass &&
					lookup.format == format &&
					lookup.computePipeline == computePipeline &&
					lookup.pipelineState)
				{
					return lookup.pipelineState;
				}
			}

			const std::string cacheKey = renderPass.id + ':' +
				(computePipeline ? "compute:" : "graphics:") +
				std::to_string(renderPass.vertexShaderBlobHash) + ':' +
				std::to_string(renderPass.fragmentShaderBlobHash) + ':' +
				std::to_string(static_cast<UINT>(format));
			{
				std::lock_guard<std::mutex> lock(gDeviceResourcesMutex);
				const auto pipelineIt = deviceResources.pipelines.find(cacheKey);
				if (pipelineIt != deviceResources.pipelines.end())
				{
					gThreadGeneratorPipelineLookups[gNextThreadGeneratorPipelineLookup] = {
						&deviceResources, &renderPass, format, computePipeline, pipelineIt->second.Get() };
					gNextThreadGeneratorPipelineLookup =
						(gNextThreadGeneratorPipelineLookup + 1) % gThreadGeneratorPipelineLookups.size();
					return pipelineIt->second.Get();
				}
				const auto errorIt = deviceResources.pipelineErrors.find(cacheKey);
				if (errorIt != deviceResources.pipelineErrors.end())
				{
					outError = errorIt->second;
					return nullptr;
				}
			}

			ComPtr<ID3D12PipelineState> pipeline;
			HRESULT result = E_FAIL;
			if (computePipeline)
			{
				D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
				description.pRootSignature = deviceResources.rootSignature.Get();
				description.CS = { renderPass.fragmentShaderBlob.data(), renderPass.fragmentShaderBlob.size() };
				result = deviceResources.device->CreateComputePipelineState(
					&description,
					IID_PPV_ARGS(&pipeline));
			}
			else
			{
				D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
				description.pRootSignature = deviceResources.rootSignature.Get();
				description.VS = { renderPass.vertexShaderBlob.data(), renderPass.vertexShaderBlob.size() };
				description.PS = { renderPass.fragmentShaderBlob.data(), renderPass.fragmentShaderBlob.size() };
				description.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
				description.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
				description.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
				description.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
				description.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
				description.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
				description.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
				description.SampleMask = UINT_MAX;
				description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
				description.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
				description.RasterizerState.DepthClipEnable = TRUE;
				description.DepthStencilState.DepthEnable = FALSE;
				description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
				description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
				description.InputLayout = { nullptr, 0 };
				description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
				description.NumRenderTargets = 1;
				description.RTVFormats[0] = format;
				description.SampleDesc = { 1, 0 };
				result = deviceResources.device->CreateGraphicsPipelineState(
					&description,
					IID_PPV_ARGS(&pipeline));
			}
			if (FAILED(result) || !pipeline)
			{
				outError = "Mip-chain pipeline creation failed with " + StringHelper::FormatHRESULT(result);
				std::lock_guard<std::mutex> lock(gDeviceResourcesMutex);
				deviceResources.pipelineErrors[cacheKey] = outError;
				return nullptr;
			}
			pipeline->SetName(computePipeline
				? L"Shader Injector Compute Mip Chain Pipeline"
				: L"Shader Injector Graphics Mip Chain Pipeline");

			std::lock_guard<std::mutex> lock(gDeviceResourcesMutex);
			auto [pipelineIt, inserted] = deviceResources.pipelines.emplace(cacheKey, pipeline);
			gThreadGeneratorPipelineLookups[gNextThreadGeneratorPipelineLookup] = {
				&deviceResources, &renderPass, format, computePipeline, pipelineIt->second.Get() };
			gNextThreadGeneratorPipelineLookup =
				(gNextThreadGeneratorPipelineLookup + 1) % gThreadGeneratorPipelineLookups.size();
			return pipelineIt->second.Get();
		}

		bool IsSlotReusable(const ExecutionSlot& slot)
		{
			if (slot.recorded || slot.retirementBlocked)
				return false;
			return !slot.retirementFence ||
				slot.retirementFence->GetCompletedValue() >= slot.retirementFenceValue;
		}

		ExecutionSlot* AcquireExecutionSlot(
			ID3D12GraphicsCommandList* commandList,
			ID3D12Device*& outDevice,
			const std::string* preferredRenderPassId = nullptr)
		{
			outDevice = nullptr;
			std::lock_guard<std::mutex> lock(gExecutionPoolMutex);
			CommandListPool& pool = gCommandListPools[commandList];
			if (!pool.device && FAILED(commandList->GetDevice(IID_PPV_ARGS(&pool.device))))
				return nullptr;
			outDevice = pool.device.Get();
			ExecutionSlot* selected = nullptr;
			if (preferredRenderPassId)
			{
				for (const std::unique_ptr<ExecutionSlot>& slot : pool.slots)
				{
					if (IsSlotReusable(*slot) &&
						slot->mipTexturesByRenderPassId.find(*preferredRenderPassId) != slot->mipTexturesByRenderPassId.end())
					{
						selected = slot.get();
						break;
					}
				}
			}
			for (const std::unique_ptr<ExecutionSlot>& slot : pool.slots)
			{
				if (selected)
					break;
				if (IsSlotReusable(*slot))
				{
					selected = slot.get();
					break;
				}
			}
			if (!selected)
			{
				pool.slots.push_back(std::make_unique<ExecutionSlot>());
				selected = pool.slots.back().get();
			}

			selected->recorded = true;
			selected->submitted = false;
			selected->recordedTextureStates.clear();
			if (pool.recordedSlots.empty())
				gRecordedCommandListCount.fetch_add(1, std::memory_order_release);
			pool.recordedSlots.push_back(selected);
			return selected;
		}

		void DiscardUnusedExecutionSlot(ID3D12GraphicsCommandList* commandList, ExecutionSlot* slot)
		{
			if (!slot)
				return;
			std::lock_guard<std::mutex> lock(gExecutionPoolMutex);
			const auto poolIt = gCommandListPools.find(commandList);
			if (poolIt == gCommandListPools.end())
				return;
			auto& recordedSlots = poolIt->second.recordedSlots;
			const auto recordedIt = std::find(recordedSlots.begin(), recordedSlots.end(), slot);
			if (recordedIt != recordedSlots.end())
			{
				recordedSlots.erase(recordedIt);
				if (recordedSlots.empty())
					gRecordedCommandListCount.fetch_sub(1, std::memory_order_release);
			}
			slot->recorded = false;
			slot->submitted = false;
			slot->recordedTextureStates.clear();
		}

		bool EnsureMipTextureResources(
			DeviceResources& deviceResources,
			const ResolvedMipPass& mipPass,
			MipTextureResources& resources,
			bool computePipeline,
			std::string& outError)
		{
			PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Timing::EnsureMipResources);
			ID3D12Device* device = deviceResources.device.Get();
			const UINT mostDetailedMip = mipPass.sourceMetadata.descriptorMostDetailedMip;
			if (mostDetailedMip >= mipPass.sourceMetadata.resourceMipLevels || mostDetailedMip >= 32 ||
				!mipPass.sourceMetadata.resourceWidth || !mipPass.sourceMetadata.resourceHeight)
			{
				outError = "The selected SRV's most-detailed mip is outside the source texture.";
				return false;
			}
			const UINT width = static_cast<UINT>((std::max<UINT64>)(1, mipPass.sourceMetadata.resourceWidth >> mostDetailedMip));
			const UINT height = (std::max)(1u, mipPass.sourceMetadata.resourceHeight >> mostDetailedMip);
			const UINT mipLevelCount = CalculateMipLevelCount(width, height);
			const DXGI_FORMAT format = static_cast<DXGI_FORMAT>(mipPass.sourceMetadata.resourceFormat);
			if (format == DXGI_FORMAT_UNKNOWN)
			{
				outError = "The source SRV format is unknown.";
				return false;
			}

			bool formatSupported = false;
			const UINT formatIndex = static_cast<UINT>(format);
			auto& formatSupportCache = computePipeline
				? deviceResources.computeMipGeneratorFormatSupport
				: deviceResources.graphicsMipGeneratorFormatSupport;
			int8_t cachedFormatSupport = formatIndex < formatSupportCache.size()
				? formatSupportCache[formatIndex].load(std::memory_order_acquire)
				: -1;
			if (cachedFormatSupport >= 0)
			{
				formatSupported = cachedFormatSupport != 0;
			}
			else
			{
				D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport{ format };
				formatSupported =
					SUCCEEDED(device->CheckFeatureSupport(
						D3D12_FEATURE_FORMAT_SUPPORT,
						&formatSupport,
						sizeof(formatSupport))) &&
					(formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D) != 0 &&
					(computePipeline
						? (formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_LOAD) != 0 &&
							(formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) != 0 &&
							(formatSupport.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0
						: (formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) != 0 &&
							(formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) != 0);
				if (formatIndex < formatSupportCache.size())
				{
					formatSupportCache[formatIndex].store(
						formatSupported ? 1 : 0,
						std::memory_order_release);
				}
			}
			if (!formatSupported)
			{
				outError = computePipeline
					? "The selected source SRV format cannot be loaded and written as a typed UAV on this device."
					: "The selected source SRV format cannot be sampled and rendered on this device.";
				return false;
			}

			const bool configurationMatches = resources.texture && resources.sourceHeap &&
				resources.fullMipChainHeap &&
				(computePipeline || resources.renderTargetHeap) &&
				resources.width == width && resources.height == height &&
				resources.mipLevelCount == mipLevelCount && resources.format == format &&
				resources.computePipeline == computePipeline;
			if (!configurationMatches)
			{
				resources = {};
				resources.width = width;
				resources.height = height;
				resources.mipLevelCount = mipLevelCount;
				resources.format = format;
				resources.computePipeline = computePipeline;
				resources.eventName = L"Shader Injector Mip Chain: " +
					StringHelper::Utf8ToWide(mipPass.renderPass->name);

				D3D12_RESOURCE_DESC textureDescription{};
				textureDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
				textureDescription.Width = width;
				textureDescription.Height = height;
				textureDescription.DepthOrArraySize = 1;
				textureDescription.MipLevels = static_cast<UINT16>(mipLevelCount);
				textureDescription.Format = format;
				textureDescription.SampleDesc = { 1, 0 };
				textureDescription.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
				textureDescription.Flags = computePipeline
					? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
					: D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

				D3D12_HEAP_PROPERTIES heapProperties{};
				heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
				heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
				heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
				heapProperties.CreationNodeMask = 1;
				heapProperties.VisibleNodeMask = 1;

				HRESULT result = device->CreateCommittedResource(
					&heapProperties,
					D3D12_HEAP_FLAG_NONE,
					&textureDescription,
					computePipeline ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_RENDER_TARGET,
					nullptr,
					IID_PPV_ARGS(&resources.texture));
				if (FAILED(result) || !resources.texture)
				{
					outError = "Mip texture creation failed with " + StringHelper::FormatHRESULT(result);
					return false;
				}

				D3D12_DESCRIPTOR_HEAP_DESC sourceHeapDescription{};
				sourceHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				sourceHeapDescription.NumDescriptors = computePipeline
					? mipLevelCount * 2
					: mipLevelCount;
				sourceHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
				result = device->CreateDescriptorHeap(&sourceHeapDescription, IID_PPV_ARGS(&resources.sourceHeap));
				if (FAILED(result) || !resources.sourceHeap)
				{
					outError = "Mip source descriptor-heap creation failed with " + StringHelper::FormatHRESULT(result);
					return false;
				}

				// CopyDescriptors sources must reside in a non-shader-visible heap.
				// Keep this reusable full-chain SRV separate from the generator's GPU tables.
				D3D12_DESCRIPTOR_HEAP_DESC fullChainHeapDescription{};
				fullChainHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				fullChainHeapDescription.NumDescriptors = 1;
				result = device->CreateDescriptorHeap(
					&fullChainHeapDescription,
					IID_PPV_ARGS(&resources.fullMipChainHeap));
				if (FAILED(result) || !resources.fullMipChainHeap)
				{
					outError = "Mip-chain SRV heap creation failed with " + StringHelper::FormatHRESULT(result);
					return false;
				}

				if (!computePipeline)
				{
					D3D12_DESCRIPTOR_HEAP_DESC renderTargetHeapDescription{};
					renderTargetHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
					renderTargetHeapDescription.NumDescriptors = mipLevelCount;
					result = device->CreateDescriptorHeap(
						&renderTargetHeapDescription,
						IID_PPV_ARGS(&resources.renderTargetHeap));
					if (FAILED(result) || !resources.renderTargetHeap)
					{
						outError = "Mip render-target descriptor-heap creation failed with " + StringHelper::FormatHRESULT(result);
						return false;
					}

					const UINT renderTargetIncrement = deviceResources.renderTargetDescriptorIncrement;
					D3D12_CPU_DESCRIPTOR_HANDLE renderTarget = resources.renderTargetHeap->GetCPUDescriptorHandleForHeapStart();
					for (UINT mipLevel = 0; mipLevel < mipLevelCount; ++mipLevel)
					{
						D3D12_RENDER_TARGET_VIEW_DESC view{};
						view.Format = format;
						view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
						view.Texture2D.MipSlice = mipLevel;
						device->CreateRenderTargetView(resources.texture.Get(), &view, renderTarget);
						renderTarget.ptr += renderTargetIncrement;
					}
				}

				const UINT sourceIncrement = deviceResources.shaderResourceDescriptorIncrement;
				D3D12_CPU_DESCRIPTOR_HANDLE sourceDescriptor =
					resources.sourceHeap->GetCPUDescriptorHandleForHeapStart();
				for (UINT destinationMip = 1; destinationMip < mipLevelCount; ++destinationMip)
				{
					sourceDescriptor.ptr += sourceIncrement;
					D3D12_SHADER_RESOURCE_VIEW_DESC sourceView{};
					sourceView.Format = format;
					sourceView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
					sourceView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
					sourceView.Texture2D.MostDetailedMip = destinationMip - 1;
					sourceView.Texture2D.MipLevels = 1;
					device->CreateShaderResourceView(resources.texture.Get(), &sourceView, sourceDescriptor);
				}
				if (computePipeline)
				{
					D3D12_CPU_DESCRIPTOR_HANDLE unorderedAccessDescriptor =
						resources.sourceHeap->GetCPUDescriptorHandleForHeapStart();
					unorderedAccessDescriptor.ptr += static_cast<SIZE_T>(mipLevelCount) * sourceIncrement;
					for (UINT mipLevel = 0; mipLevel < mipLevelCount; ++mipLevel)
					{
						D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
						view.Format = format;
						view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
						view.Texture2D.MipSlice = mipLevel;
						device->CreateUnorderedAccessView(
							resources.texture.Get(),
							nullptr,
							&view,
							unorderedAccessDescriptor);
						unorderedAccessDescriptor.ptr += sourceIncrement;
					}
				}

				resources.fullMipChainDescriptor = resources.fullMipChainHeap->GetCPUDescriptorHandleForHeapStart();
				resources.texture->SetName(resources.eventName.c_str());
				ShaderResource::CatalogEntry catalogEntry{};
				catalogEntry.id = RenderPass::MipChainOutputResourceId(*mipPass.renderPass);
				catalogEntry.name = mipPass.renderPass->name + " Mip Chain";
				catalogEntry.origin = ShaderResource::ResourceOrigin::Runtime;
				catalogEntry.lifetime = ShaderResource::ResourceLifetime::Persistent;
				catalogEntry.ownerRenderPassId = mipPass.renderPass->id;
				catalogEntry.dimension = ShaderResource::TextureDimension::Texture2D;
				catalogEntry.width = width;
				catalogEntry.height = height;
				catalogEntry.mipLevels = mipLevelCount;
				catalogEntry.format = static_cast<uint32_t>(format);
				catalogEntry.resident = true;
				catalogEntry.status = "Generated mip chain";
				ShaderResourceCatalog::Upsert(catalogEntry);
				PerformanceMetrics::Increment(PerformanceMetrics::Counter::MipResourceCreated);
			}
			else
			{
				PerformanceMetrics::Increment(PerformanceMetrics::Counter::MipResourceReused);
			}

			if (!resources.fullMipChainDescriptorInitialized ||
				resources.shader4ComponentMapping != mipPass.sourceMetadata.descriptorShader4ComponentMapping)
			{
				D3D12_SHADER_RESOURCE_VIEW_DESC fullMipChainView{};
				fullMipChainView.Format = format;
				fullMipChainView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				fullMipChainView.Shader4ComponentMapping = mipPass.sourceMetadata.descriptorShader4ComponentMapping;
				fullMipChainView.Texture2D.MostDetailedMip = 0;
				fullMipChainView.Texture2D.MipLevels = mipLevelCount;
				device->CreateShaderResourceView(
					resources.texture.Get(),
					&fullMipChainView,
					resources.fullMipChainDescriptor);
				resources.shader4ComponentMapping = mipPass.sourceMetadata.descriptorShader4ComponentMapping;
				resources.fullMipChainDescriptorInitialized = true;
			}

			D3D12_CPU_DESCRIPTOR_HANDLE sourceDescriptor = resources.sourceHeap->GetCPUDescriptorHandleForHeapStart();
			device->CopyDescriptorsSimple(
				1,
				sourceDescriptor,
				mipPass.sourceDescriptor,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			return true;
		}

		void RestoreRootArguments(
			ID3D12GraphicsCommandList* commandList,
			const GraphicsStateSnapshot& gameState,
			bool computePipeline,
			bool includeDescriptorTables)
		{
			for (const RootArgumentSnapshot& binding : gameState.rootBindings)
			{
				if (binding.type == RootArgumentType::DescriptorTable)
				{
					if (includeDescriptorTables)
					{
						if (computePipeline)
							commandList->SetComputeRootDescriptorTable(binding.rootParameterIndex, { binding.value });
						else
							commandList->SetGraphicsRootDescriptorTable(binding.rootParameterIndex, { binding.value });
					}
				}
				else if (binding.type == RootArgumentType::Constants)
				{
					if (!binding.constants.empty())
					{
						if (computePipeline)
							commandList->SetComputeRoot32BitConstants(
								binding.rootParameterIndex,
								static_cast<UINT>(binding.constants.size()),
								binding.constants.data(),
								0);
						else
							commandList->SetGraphicsRoot32BitConstants(
								binding.rootParameterIndex,
								static_cast<UINT>(binding.constants.size()),
								binding.constants.data(),
								0);
					}
				}
				else if (binding.type == RootArgumentType::ConstantBufferView)
				{
					if (computePipeline)
						commandList->SetComputeRootConstantBufferView(binding.rootParameterIndex, binding.value);
					else
						commandList->SetGraphicsRootConstantBufferView(binding.rootParameterIndex, binding.value);
				}
				else if (binding.type == RootArgumentType::ShaderResourceView)
				{
					if (computePipeline)
						commandList->SetComputeRootShaderResourceView(binding.rootParameterIndex, binding.value);
					else
						commandList->SetGraphicsRootShaderResourceView(binding.rootParameterIndex, binding.value);
				}
				else if (binding.type == RootArgumentType::UnorderedAccessView)
				{
					if (computePipeline)
						commandList->SetComputeRootUnorderedAccessView(binding.rootParameterIndex, binding.value);
					else
						commandList->SetGraphicsRootUnorderedAccessView(binding.rootParameterIndex, binding.value);
				}
			}
		}

		void RestoreGameState(
			ID3D12GraphicsCommandList* commandList,
			const GraphicsStateSnapshot& gameState,
			bool computePipeline)
		{
			if (computePipeline)
				commandList->SetComputeRootSignature(gameState.rootSignature);
			else
				commandList->SetGraphicsRootSignature(gameState.rootSignature);
			commandList->SetPipelineState(gameState.pipelineState);
			if (!computePipeline)
			{
				commandList->IASetPrimitiveTopology(gameState.primitiveTopology);
				if (!gameState.viewports.empty())
					commandList->RSSetViewports(static_cast<UINT>(gameState.viewports.size()), gameState.viewports.data());
				if (!gameState.scissorRectangles.empty())
					commandList->RSSetScissorRects(
						static_cast<UINT>(gameState.scissorRectangles.size()),
						gameState.scissorRectangles.data());
				commandList->OMSetRenderTargets(
					static_cast<UINT>(gameState.renderTargets.size()),
					gameState.renderTargets.empty() ? nullptr : gameState.renderTargets.data(),
					FALSE,
					gameState.depthStencil.ptr ? &gameState.depthStencil : nullptr);
			}

			std::array<ID3D12DescriptorHeap*, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> descriptorHeaps{};
			UINT descriptorHeapCount = 0;
			for (const DescriptorHeapBinding& heap : gameState.descriptorHeaps)
			{
				if (heap.heap && descriptorHeapCount < descriptorHeaps.size())
					descriptorHeaps[descriptorHeapCount++] = heap.heap;
			}
			commandList->SetDescriptorHeaps(
				descriptorHeapCount,
				descriptorHeapCount ? descriptorHeaps.data() : nullptr);
			RestoreRootArguments(commandList, gameState, computePipeline, true);
		}

		bool RecordMipGeneration(
			const ResolvedMipPass& mipPass,
			ID3D12GraphicsCommandList* commandList,
			DeviceResources& deviceResources,
			ID3D12PipelineState* pipeline,
			bool computePipeline,
			std::string& outError)
		{
			PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Timing::RecordMipGeneration);
			MipTextureResources& resources = *mipPass.resources;
			if (resources.allSubresourcesShaderReadable)
			{
				std::array<D3D12_RESOURCE_BARRIER, sizeof(UINT) * 8> barriers{};
				for (UINT mipLevel = 0; mipLevel < resources.mipLevelCount; ++mipLevel)
				{
					D3D12_RESOURCE_BARRIER& barrier = barriers[mipLevel];
					barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					barrier.Transition.pResource = resources.texture.Get();
					barrier.Transition.Subresource = mipLevel;
					barrier.Transition.StateBefore = computePipeline
						? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
						: D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
					barrier.Transition.StateAfter = computePipeline
						? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
						: D3D12_RESOURCE_STATE_RENDER_TARGET;
				}
				commandList->ResourceBarrier(resources.mipLevelCount, barriers.data());
			}

			const UINT sourceIncrement = deviceResources.shaderResourceDescriptorIncrement;
			const UINT renderTargetIncrement = deviceResources.renderTargetDescriptorIncrement;
			D3D12_GPU_DESCRIPTOR_HANDLE source = resources.sourceHeap->GetGPUDescriptorHandleForHeapStart();
			D3D12_GPU_DESCRIPTOR_HANDLE unorderedAccess = source;
			unorderedAccess.ptr += static_cast<UINT64>(resources.mipLevelCount) * sourceIncrement;
			D3D12_CPU_DESCRIPTOR_HANDLE renderTarget{};
			if (!computePipeline)
				renderTarget = resources.renderTargetHeap->GetCPUDescriptorHandleForHeapStart();
			ID3D12DescriptorHeap* sourceHeap = resources.sourceHeap.Get();
			commandList->SetDescriptorHeaps(1, &sourceHeap);
			if (computePipeline)
				commandList->SetComputeRootSignature(deviceResources.rootSignature.Get());
			else
				commandList->SetGraphicsRootSignature(deviceResources.rootSignature.Get());
			commandList->SetPipelineState(pipeline);
			if (!computePipeline)
				commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			uint64_t generatedTexels = 0;
			for (UINT destinationMip = 0; destinationMip < resources.mipLevelCount; ++destinationMip)
			{
				const UINT destinationWidth = (std::max)(1u, resources.width >> destinationMip);
				const UINT destinationHeight = (std::max)(1u, resources.height >> destinationMip);
				generatedTexels += static_cast<uint64_t>(destinationWidth) * destinationHeight;
				const UINT sourceWidth = destinationMip == 0
					? resources.width
					: (std::max)(1u, resources.width >> (destinationMip - 1));
				const UINT sourceHeight = destinationMip == 0
					? resources.height
					: (std::max)(1u, resources.height >> (destinationMip - 1));
				const UINT constants[4] = { destinationMip == 0 ? 1u : 0u, sourceWidth, sourceHeight, 0u };
				if (computePipeline)
				{
					commandList->SetComputeRootDescriptorTable(0, source);
					commandList->SetComputeRoot32BitConstants(1, 4, constants, 0);
					commandList->SetComputeRootDescriptorTable(2, unorderedAccess);
					commandList->Dispatch(
						(destinationWidth + 7u) / 8u,
						(destinationHeight + 7u) / 8u,
						1);
				}
				else
				{
					D3D12_VIEWPORT viewport{};
					viewport.Width = static_cast<float>(destinationWidth);
					viewport.Height = static_cast<float>(destinationHeight);
					viewport.MaxDepth = 1.0f;
					const D3D12_RECT scissor{ 0, 0, static_cast<LONG>(destinationWidth), static_cast<LONG>(destinationHeight) };

					commandList->SetGraphicsRootDescriptorTable(0, source);
					commandList->SetGraphicsRoot32BitConstants(1, 4, constants, 0);
					commandList->RSSetViewports(1, &viewport);
					commandList->RSSetScissorRects(1, &scissor);
					commandList->OMSetRenderTargets(1, &renderTarget, FALSE, nullptr);
					commandList->DrawInstanced(3, 1, 0, 0);
				}

				if (computePipeline)
				{
					D3D12_RESOURCE_BARRIER barriers[2]{};
					barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
					barriers[0].UAV.pResource = resources.texture.Get();
					barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					barriers[1].Transition.pResource = resources.texture.Get();
					barriers[1].Transition.Subresource = destinationMip;
					barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
					barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
					commandList->ResourceBarrier(2, barriers);
				}
				else
				{
					D3D12_RESOURCE_BARRIER barrier{};
					barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					barrier.Transition.pResource = resources.texture.Get();
					barrier.Transition.Subresource = destinationMip;
					barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
					barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
					commandList->ResourceBarrier(1, &barrier);
				}

				source.ptr += sourceIncrement;
				if (computePipeline)
					unorderedAccess.ptr += sourceIncrement;
				else
					renderTarget.ptr += renderTargetIncrement;
			}
			PerformanceMetrics::Increment(
				PerformanceMetrics::Counter::MipLevelDraw,
				resources.mipLevelCount);
			PerformanceMetrics::Increment(PerformanceMetrics::Counter::MipTexels, generatedTexels);
			resources.allSubresourcesShaderReadable = true;
			return true;
		}

		bool BuildAndBindNativeDescriptorHeap(
			ID3D12Device* device,
			ID3D12GraphicsCommandList* commandList,
			ExecutionSlot& slot,
			const GraphicsStateSnapshot& gameState,
			const std::vector<ResolvedMipPass*>& successfulPasses,
			bool computePipeline,
			std::string& outError)
		{
			PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Timing::BindMipDescriptors);
			UINT maximumDescriptors = 1;
			for (const ResolvedMipPass* mipPass : successfulPasses)
				maximumDescriptors = (std::max)(maximumDescriptors, mipPass->renderPass->maximumTrackedDescriptors);
			if (slot.cachedLayoutRootSignature != gameState.rootSignature ||
				slot.cachedLayoutMaximumDescriptors != maximumDescriptors)
			{
				slot.cachedLayoutRootSignature = gameState.rootSignature;
				slot.cachedLayoutMaximumDescriptors = maximumDescriptors;
				slot.cachedDescriptorTableLayouts.clear();
				RenderPassResourceRegistry::GetDescriptorTableLayouts(
					gameState.rootSignature,
					maximumDescriptors,
					slot.cachedDescriptorTableLayouts);
			}
			const std::vector<RenderPassResourceRegistry::DescriptorTableLayout>& layouts =
				slot.cachedDescriptorTableLayouts;
			if (layouts.empty())
			{
				outError = "The target root-signature descriptor-table layout is unavailable.";
				return false;
			}

			thread_local std::vector<ActiveDescriptorTable> activeTables;
			activeTables.clear();
			UINT nativeDescriptorCount = 0;
			for (const RootArgumentSnapshot& binding : gameState.rootBindings)
			{
				if (binding.type != RootArgumentType::DescriptorTable)
					continue;

				const auto layoutIt = std::find_if(layouts.begin(), layouts.end(), [&](const auto& layout)
				{
					return layout.rootParameterIndex == binding.rootParameterIndex;
				});
				if (layoutIt == layouts.end() || layoutIt->containsUnboundedRange)
				{
					outError = "An active target descriptor table is unbounded or could not be described safely.";
					return false;
				}

				const DescriptorHeapBinding* sourceHeap = FindHeapForGpuHandle(
					gameState,
					{ binding.value },
					layoutIt->heapType);
				if (!sourceHeap)
				{
					outError = std::string("An active ") + (computePipeline ? "compute" : "graphics") +
						" descriptor table is outside the currently bound heaps.";
					return false;
				}

			const UINT64 tableByteOffset = binding.value - sourceHeap->gpuStart.ptr;
				const UINT64 tableByteSize = static_cast<UINT64>(layoutIt->descriptorCount) * sourceHeap->descriptorIncrementSize;
				const UINT64 heapByteSize = static_cast<UINT64>(sourceHeap->descriptorCount) * sourceHeap->descriptorIncrementSize;
				if (tableByteOffset + tableByteSize > heapByteSize)
				{
					outError = "An active graphics descriptor table extends beyond its bound heap.";
					return false;
				}

				ActiveDescriptorTable table{};
				table.rootParameterIndex = binding.rootParameterIndex;
				table.heapType = layoutIt->heapType;
				table.descriptorCount = layoutIt->descriptorCount;
				table.originalGpuHandle = { binding.value };
				table.originalCpuHandle = { sourceHeap->cpuStart.ptr + tableByteOffset };
				if (table.heapType == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
				{
					table.customHeapOffset = nativeDescriptorCount;
					nativeDescriptorCount += table.descriptorCount;
				}
				activeTables.push_back(table);
			}

			if (!nativeDescriptorCount)
			{
				outError = "The target draw has no active CBV/SRV/UAV descriptor tables to clone.";
				return false;
			}

			if (!slot.nativeDescriptorHeap || slot.nativeDescriptorCapacity < nativeDescriptorCount)
			{
				D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
				heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
				heapDescription.NumDescriptors = nativeDescriptorCount;
				heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
				slot.nativeDescriptorHeap.Reset();
				const HRESULT result = device->CreateDescriptorHeap(
					&heapDescription,
					IID_PPV_ARGS(&slot.nativeDescriptorHeap));
				if (FAILED(result) || !slot.nativeDescriptorHeap)
				{
					outError = "Native descriptor clone heap creation failed with " + StringHelper::FormatHRESULT(result);
					return false;
				}
				slot.nativeDescriptorCapacity = nativeDescriptorCount;
			}

			const UINT increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			const D3D12_CPU_DESCRIPTOR_HANDLE customCpuStart = slot.nativeDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			const D3D12_GPU_DESCRIPTOR_HANDLE customGpuStart = slot.nativeDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
			for (const ActiveDescriptorTable& table : activeTables)
			{
				if (table.heapType != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
					continue;
				device->CopyDescriptorsSimple(
					table.descriptorCount,
					{ customCpuStart.ptr + static_cast<SIZE_T>(table.customHeapOffset) * increment },
					table.originalCpuHandle,
					D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			}

			for (const ResolvedMipPass* mipPass : successfulPasses)
			{
				const auto tableIt = std::find_if(activeTables.begin(), activeTables.end(), [&](const auto& table)
				{
					return table.rootParameterIndex == mipPass->bindingLocation.rootParameterIndex;
				});
				if (tableIt == activeTables.end() || tableIt->heapType != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ||
					mipPass->bindingLocation.tableOffset >= tableIt->descriptorCount)
				{
					outError = "The selected mip source could not be mapped into the cloned descriptor tables.";
					return false;
				}

				const UINT descriptorOffset = tableIt->customHeapOffset + mipPass->bindingLocation.tableOffset;
				device->CopyDescriptorsSimple(
					1,
					{ customCpuStart.ptr + static_cast<SIZE_T>(descriptorOffset) * increment },
					mipPass->resources->fullMipChainDescriptor,
					D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			}

			ID3D12DescriptorHeap* samplerHeap = nullptr;
			for (const DescriptorHeapBinding& heap : gameState.descriptorHeaps)
			{
				if (heap.type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
				{
					samplerHeap = heap.heap;
					break;
				}
			}
			ID3D12DescriptorHeap* heaps[2] = { slot.nativeDescriptorHeap.Get(), samplerHeap };
			commandList->SetDescriptorHeaps(samplerHeap ? 2u : 1u, heaps);
			for (const ActiveDescriptorTable& table : activeTables)
			{
				const D3D12_GPU_DESCRIPTOR_HANDLE handle = table.heapType == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
					? D3D12_GPU_DESCRIPTOR_HANDLE{
						customGpuStart.ptr + static_cast<UINT64>(table.customHeapOffset) * increment }
					: table.originalGpuHandle;
				if (computePipeline)
					commandList->SetComputeRootDescriptorTable(table.rootParameterIndex, handle);
				else
					commandList->SetGraphicsRootDescriptorTable(table.rootParameterIndex, handle);
			}
			return true;
		}
	}

	bool GenerateRuntimeMipChain(
		const RenderPass::RenderPassDisk& renderPass,
		ID3D12GraphicsCommandList* commandList,
		const RenderPassTexturePool::TextureView& source,
		const GraphicsStateSnapshot& gameState,
		const GraphicsStateSnapshot& oppositePipelineState,
		bool computePipeline,
		RenderPassTexturePool::TextureView& outTexture,
		std::string& outError)
	{
		outTexture = {};
		outError.clear();
		if (!commandList || !gameState.rootSignature || !gameState.pipelineState ||
			(!computePipeline && (gameState.primitiveTopology == D3D_PRIMITIVE_TOPOLOGY_UNDEFINED ||
				gameState.viewports.empty() || gameState.scissorRectangles.empty())))
		{
			outError = "Runtime mip generation requires complete restorable target state.";
			return false;
		}
		if (!RenderPass::HasCompiledShaders(renderPass))
		{
			outError = "Mip-chain shaders have not been compiled and loaded.";
			return false;
		}
		if (!source.resource || !source.shaderResourceView.ptr ||
			source.description.dimension != ShaderResource::TextureDimension::Texture2D ||
			source.description.arraySize != 1 || source.description.sampleCount != 1)
		{
			outError = "Runtime mip source must be a resident, non-array, non-MSAA Texture2D SRV.";
			return false;
		}

		ResolvedMipPass mipPass{};
		mipPass.renderPass = &renderPass;
		mipPass.sourceResource = source.resource;
		mipPass.sourceDescriptor = source.shaderResourceView;
		mipPass.sourceMetadata.resourceWidth = source.description.width;
		mipPass.sourceMetadata.resourceHeight = source.description.height;
		mipPass.sourceMetadata.resourceMipLevels = source.description.mipLevels;
		mipPass.sourceMetadata.resourceFormat = static_cast<uint32_t>(source.description.format);
		mipPass.sourceMetadata.descriptorShader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		ID3D12Device* device = nullptr;
		ExecutionSlot* slot = AcquireExecutionSlot(commandList, device, &renderPass.id);
		if (!slot || !device)
		{
			outError = "Could not query the D3D12 device for the runtime mip chain.";
			DiscardUnusedExecutionSlot(commandList, slot);
			return false;
		}

		HookD3D12::ScopedRenderPassInjection injectionScope;
		DeviceResources* deviceResources = GetDeviceResources(device, outError);
		mipPass.resources = &slot->mipTexturesByRenderPassId[renderPass.id];
		if (!deviceResources || !EnsureMipTextureResources(
			*deviceResources, mipPass, *mipPass.resources, computePipeline, outError))
		{
			DiscardUnusedExecutionSlot(commandList, slot);
			return false;
		}
		ID3D12PipelineState* pipeline = GetGeneratorPipeline(
			*deviceResources, renderPass, mipPass.resources->format, computePipeline, outError);
		if (!pipeline)
		{
			DiscardUnusedExecutionSlot(commandList, slot);
			return false;
		}

		MipTextureResources& resources = *mipPass.resources;
		slot->recordedTextureStates.push_back({ &resources, resources.allSubresourcesShaderReadable });
		commandList->BeginEvent(0, resources.eventName.c_str(),
			static_cast<UINT>((resources.eventName.size() + 1) * sizeof(wchar_t)));
		const bool succeeded = RecordMipGeneration(
			mipPass, commandList, *deviceResources, pipeline, computePipeline, outError);
		RestoreGameState(commandList, gameState, computePipeline);
		RestoreRootArguments(commandList, oppositePipelineState, !computePipeline, true);
		commandList->EndEvent();
		if (!succeeded)
			return false;

		// The fenced execution slot owns the allocation until all recorded GPU uses
		// finish. Consumers only borrow its read-only view for this graph execution.
		outTexture.resource = resources.texture;
		outTexture.shaderViewHeap = resources.fullMipChainHeap;
		outTexture.shaderResourceView = resources.fullMipChainDescriptor;
		outTexture.description = source.description;
		outTexture.description.width = resources.width;
		outTexture.description.height = resources.height;
		outTexture.description.mipLevels = resources.mipLevelCount;
		outTexture.description.flags = computePipeline
			? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		outTexture.initialState = computePipeline
			? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		return true;
	}

	void PrepareForTargetDraw(
		const std::vector<const RenderPass::RenderPassDisk*>& renderPasses,
		ID3D12GraphicsCommandList* commandList,
		const GraphicsStateSnapshot& gameState,
		bool computePipeline,
		std::vector<ExecutionResult>& outResults)
	{
		outResults.clear();
		outResults.reserve(renderPasses.size());
		for (const RenderPass::RenderPassDisk* renderPass : renderPasses)
			outResults.push_back({ renderPass, true, false, {} });

		if (!commandList || !gameState.rootSignature || !gameState.pipelineState ||
			(!computePipeline &&
				(gameState.primitiveTopology == D3D_PRIMITIVE_TOPOLOGY_UNDEFINED ||
					gameState.viewports.empty() || gameState.scissorRectangles.empty())))
		{
			for (ExecutionResult& result : outResults)
				result.error = computePipeline
					? "The target dispatch does not have complete restorable compute state."
					: "The target draw does not have complete restorable graphics state.";
			return;
		}

		thread_local std::vector<ResolvedMipPass> resolvedPasses;
		resolvedPasses.clear();
		resolvedPasses.reserve(renderPasses.size());
		for (size_t passIndex = 0; passIndex < renderPasses.size(); ++passIndex)
		{
			if (!renderPasses[passIndex])
			{
				outResults[passIndex].error = "Render Pass configuration is unavailable.";
				continue;
			}
			if (RenderPass::ResolveExecutionMode(*renderPasses[passIndex]) !=
				(computePipeline ? RenderPass::ExecutionMode::Compute : RenderPass::ExecutionMode::FullscreenPixel))
			{
				outResults[passIndex].attempted = false;
				continue;
			}

			ResolvedMipPass resolved{};
			resolved.renderPass = renderPasses[passIndex];
			resolved.resultIndex = passIndex;
			if (!ResolveMipPass(
				*renderPasses[passIndex],
				gameState,
				computePipeline,
				resolved,
				outResults[passIndex].error))
				continue;
			resolvedPasses.push_back(std::move(resolved));
		}
		if (resolvedPasses.empty())
			return;

		ID3D12Device* device = nullptr;
		ExecutionSlot* slot = AcquireExecutionSlot(commandList, device);
		if (!slot || !device)
		{
			for (ResolvedMipPass& mipPass : resolvedPasses)
				outResults[mipPass.resultIndex].error =
					"Could not query the D3D12 device from the target command list.";
			return;
		}
		std::string deviceError;
		DeviceResources* deviceResources = GetDeviceResources(device, deviceError);
		if (!deviceResources)
		{
			for (ResolvedMipPass& mipPass : resolvedPasses)
				outResults[mipPass.resultIndex].error = deviceError;
			DiscardUnusedExecutionSlot(commandList, slot);
			return;
		}

		thread_local std::vector<ResolvedMipPass*> successfulPasses;
		successfulPasses.clear();
		successfulPasses.reserve(resolvedPasses.size());
		{
			HookD3D12::ScopedRenderPassInjection injectionScope;
			constexpr wchar_t eventName[] = L"Shader Injector Mip Chain";
			commandList->BeginEvent(0, eventName, static_cast<UINT>(sizeof(eventName)));
			for (ResolvedMipPass& mipPass : resolvedPasses)
			{
				ExecutionResult& result = outResults[mipPass.resultIndex];
				mipPass.resources = &slot->mipTexturesByRenderPassId[mipPass.renderPass->id];
				if (!EnsureMipTextureResources(
					*deviceResources,
					mipPass,
					*mipPass.resources,
					computePipeline,
					result.error))
					continue;

				ID3D12PipelineState* pipeline = GetGeneratorPipeline(
					*deviceResources,
					*mipPass.renderPass,
					mipPass.resources->format,
					computePipeline,
					result.error);
				if (!pipeline)
					continue;
				slot->recordedTextureStates.push_back({
					mipPass.resources,
					mipPass.resources->allSubresourcesShaderReadable });
				if (!RecordMipGeneration(
					mipPass,
					commandList,
					*deviceResources,
					pipeline,
					computePipeline,
					result.error))
				{
					continue;
				}
				result.succeeded = true;
				successfulPasses.push_back(&mipPass);
			}

			if (!successfulPasses.empty())
			{
				RestoreGameState(commandList, gameState, computePipeline);
				std::string bindingError;
				if (!BuildAndBindNativeDescriptorHeap(
					device,
					commandList,
					*slot,
					gameState,
					successfulPasses,
					computePipeline,
					bindingError))
				{
					for (ResolvedMipPass* mipPass : successfulPasses)
					{
						ExecutionResult& result = outResults[mipPass->resultIndex];
						result.succeeded = false;
						result.error = bindingError;
					}
					RestoreGameState(commandList, gameState, computePipeline);
					successfulPasses.clear();
				}
			}
			commandList->EndEvent();
		}
		if (slot->recordedTextureStates.empty())
			DiscardUnusedExecutionSlot(commandList, slot);
	}

	void RestoreAfterTargetDraw(
		ID3D12GraphicsCommandList* commandList,
		const GraphicsStateSnapshot& gameState,
		bool computePipeline)
	{
		HookD3D12::ScopedRenderPassInjection injectionScope;
		std::array<ID3D12DescriptorHeap*, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> descriptorHeaps{};
		UINT descriptorHeapCount = 0;
		for (const DescriptorHeapBinding& heap : gameState.descriptorHeaps)
		{
			if (heap.heap && descriptorHeapCount < descriptorHeaps.size())
				descriptorHeaps[descriptorHeapCount++] = heap.heap;
		}
		commandList->SetDescriptorHeaps(
			descriptorHeapCount,
			descriptorHeapCount ? descriptorHeaps.data() : nullptr);
		RestoreRootArguments(commandList, gameState, computePipeline, true);
	}

	bool HasRecordedCommandListWork()
	{
		return gRecordedCommandListCount.load(std::memory_order_acquire) != 0;
	}

	void ResetCommandListRecording(ID3D12GraphicsCommandList* commandList)
	{
		// Reset is one of the game's hottest command-list paths during a fresh
		// shader-cache build. Do not touch the mip pool mutex until a mip pass has
		// actually recorded work on at least one command list.
		if (!commandList || !HasRecordedCommandListWork())
			return;
		std::lock_guard<std::mutex> lock(gExecutionPoolMutex);
		const auto poolIt = gCommandListPools.find(commandList);
		if (poolIt == gCommandListPools.end())
			return;
		for (ExecutionSlot* slot : poolIt->second.recordedSlots)
		{
			if (!slot)
				continue;
			if (!slot->submitted)
			{
				for (const ExecutionSlot::RecordedTextureState& recordedState : slot->recordedTextureStates)
				{
					if (recordedState.resources)
					{
						recordedState.resources->allSubresourcesShaderReadable =
							recordedState.allSubresourcesShaderReadable;
					}
				}
			}
			// A discarded recording never reached the GPU, so its slot is already
			// safe to reuse. Keep its allocations warm for the next recording.
			slot->recordedTextureStates.clear();
			slot->recorded = false;
		}
		if (!poolIt->second.recordedSlots.empty())
			gRecordedCommandListCount.fetch_sub(1, std::memory_order_acq_rel);
		poolIt->second.recordedSlots.clear();
	}

	void NotifyCommandListsSubmitted(
		ID3D12CommandQueue* commandQueue,
		UINT commandListCount,
		ID3D12CommandList* const* commandLists)
	{
		if (gRecordedCommandListCount.load(std::memory_order_acquire) == 0 ||
			!commandQueue || !commandLists || !commandListCount ||
			commandQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
		{
			return;
		}

		std::lock_guard<std::mutex> lock(gExecutionPoolMutex);
		thread_local std::vector<ExecutionSlot*> submittedSlots;
		thread_local std::unordered_set<ExecutionSlot*> uniqueSlots;
		submittedSlots.clear();
		uniqueSlots.clear();
		submittedSlots.reserve(commandListCount);
		uniqueSlots.reserve(commandListCount);
		for (UINT commandListIndex = 0; commandListIndex < commandListCount; ++commandListIndex)
		{
			ID3D12GraphicsCommandList* graphicsCommandList =
				reinterpret_cast<ID3D12GraphicsCommandList*>(commandLists[commandListIndex]);
			const auto poolIt = gCommandListPools.find(graphicsCommandList);
			if (poolIt == gCommandListPools.end())
				continue;
			for (ExecutionSlot* slot : poolIt->second.recordedSlots)
			{
				if (slot && uniqueSlots.insert(slot).second)
					submittedSlots.push_back(slot);
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
				for (ExecutionSlot* slot : submittedSlots)
				{
					slot->submitted = true;
					slot->retirementBlocked = true;
				}
				ShaderInjectorIO::WriteToLogFileError(
					"RenderPassMipChain->NotifyCommandListsSubmitted: could not create the retirement fence");
				return;
			}
		}

		const UINT64 fenceValue = ++queueFence.nextValue;
		const HRESULT result = commandQueue->Signal(queueFence.fence.Get(), fenceValue);
		if (FAILED(result))
		{
			for (ExecutionSlot* slot : submittedSlots)
			{
				slot->submitted = true;
				slot->retirementBlocked = true;
			}
			ShaderInjectorIO::WriteToLogFileError(
				"RenderPassMipChain->NotifyCommandListsSubmitted: queue signal failed with " +
				StringHelper::FormatHRESULT(result));
			return;
		}

		for (ExecutionSlot* slot : submittedSlots)
		{
			slot->retirementFence = queueFence.fence;
			slot->retirementFenceValue = fenceValue;
			slot->submitted = true;
			slot->retirementBlocked = false;
		}
	}

	void ReleaseResources()
	{
		ShaderResourceCatalog::ClearRuntimeResources();
		{
			std::lock_guard<std::mutex> lock(gExecutionPoolMutex);
			gCommandListPools.clear();
			gQueueFences.clear();
			gRecordedCommandListCount.store(0, std::memory_order_release);
		}
		std::lock_guard<std::mutex> lock(gDeviceResourcesMutex);
		gDeviceResources.clear();
		gThreadDeviceResourcesLookup = {};
		gThreadGeneratorPipelineLookups = {};
		gNextThreadGeneratorPipelineLookup = 0;
		gThreadMipBindingLookups = {};
		gNextThreadMipBindingLookup = 0;
	}
}
