#include "RenderPassRuntime.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <iterator>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "HookD3D12.h"
#include "RenderPassResourceRegistry.h"
#include "RenderPassExecutor.h"
#include "RenderPassMipChain.h"
#include "ShaderInjectorIO.h"
#include "StringHelper.h"

namespace RenderPassRuntime
{
	namespace
	{
		struct ShaderTargetBinding
		{
			std::string modifiedShaderId;
			std::string name;
			uint64_t hash = 0;
			ShaderTarget::ShaderType type = ShaderTarget::Unknown;
			PipelineOutputState outputState;
		};

		using ShaderTargetBindingMap = std::unordered_map<ID3D12PipelineState*, ShaderTargetBinding>;

		struct RenderPassConfigurationSnapshot
		{
			std::vector<RenderPass::RenderPassDisk> renderPasses;
		};

		struct DescriptorHeapState
		{
			ID3D12DescriptorHeap* heap = nullptr;
			D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
			UINT descriptorCount = 0;
			UINT descriptorIncrementSize = 0;
			D3D12_CPU_DESCRIPTOR_HANDLE cpuStart{};
			D3D12_GPU_DESCRIPTOR_HANDLE gpuStart{};
		};

		struct CommandListRenderState
		{
			ID3D12PipelineState* pipelineState = nullptr;
			ID3D12PipelineState* boundPipelineState = nullptr;
			ID3D12RootSignature* graphicsRootSignature = nullptr;
			ID3D12RootSignature* computeRootSignature = nullptr;
			D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
			std::vector<DescriptorHeapState> descriptorHeaps;
			std::unordered_map<uint64_t, RenderPass::ResourceBindingDiagnostic> rootBindings;
			std::vector<RenderPass::ResourceBindingDiagnostic> inputBindings;
			std::vector<RenderPass::ResourceBindingDiagnostic> outputBindings;
			std::vector<D3D12_VIEWPORT> viewports;
			std::vector<D3D12_RECT> scissorRectangles;
			UINT descriptorIncrementSizes[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES]{};
			bool descriptorIncrementSizesInitialized = false;
			uint32_t graphicsExecutionBoundaryMask = 0;
			uint32_t computeExecutionBoundaryMask = 0;
			uint64_t executionTrackingGeneration = 0;
		};

		const RenderPassConfigurationSnapshot gEmptyConfigurationSnapshot;
		const ShaderTargetBindingMap gEmptyShaderTargetBindingMap;
		std::atomic<const RenderPassConfigurationSnapshot*> gPublishedConfiguration = &gEmptyConfigurationSnapshot;
		std::atomic<const ShaderTargetBindingMap*> gPublishedShaderTargetBindings = &gEmptyShaderTargetBindingMap;
		std::atomic<bool> gHasEnabledRenderPasses = false;
		std::atomic<bool> gHasEnabledMipChainPasses = false;
		std::atomic<bool> gHasExecutableRenderPassBinding = false;
		std::atomic<bool> gHasExecutableMipChainBinding = false;
		std::atomic<bool> gResourceTrackingRequired = false;
		std::atomic<uint32_t> gGraphicsExecutionBoundaryMask = 0;
		std::atomic<uint32_t> gComputeExecutionBoundaryMask = 0;
		std::atomic<uint64_t> gExecutionTrackingGeneration = 1;

		std::mutex gConfigurationPublishMutex;
		std::vector<std::unique_ptr<const RenderPassConfigurationSnapshot>> gOwnedConfigurationSnapshots;
		std::vector<std::unique_ptr<const ShaderTargetBindingMap>> gOwnedShaderTargetBindingSnapshots;
		ShaderTargetBindingMap gPendingShaderTargetBindings;

		std::mutex gCommandListRegistryMutex;
		std::unordered_map<ID3D12GraphicsCommandList*, std::unique_ptr<CommandListRenderState>> gCommandListStates;
		thread_local ID3D12GraphicsCommandList* gCachedCommandList = nullptr;
		thread_local CommandListRenderState* gCachedCommandListState = nullptr;

		std::mutex gDiagnosticsMutex;
		std::unordered_map<std::string, RenderPass::RuntimeDiagnostics> gDiagnosticsByRenderPassId;
		std::unordered_set<std::string> gPendingResourceSnapshotIds;

		bool HasLinkedShaderTargetBinding(
			const RenderPassConfigurationSnapshot& configuration,
			const ShaderTargetBindingMap& shaderTargetBindings)
		{
			for (const RenderPass::RenderPassDisk& renderPass : configuration.renderPasses)
			{
				if (!renderPass.enabled || renderPass.modifiedShaderId.empty())
					continue;

				for (const auto& shaderTargetBinding : shaderTargetBindings)
				{
					if (shaderTargetBinding.second.modifiedShaderId == renderPass.modifiedShaderId)
						return true;
				}
			}

			return false;
		}

		bool HasLinkedMipChainBinding(
			const RenderPassConfigurationSnapshot& configuration,
			const ShaderTargetBindingMap& shaderTargetBindings)
		{
			for (const RenderPass::RenderPassDisk& renderPass : configuration.renderPasses)
			{
				if (!renderPass.enabled || renderPass.type != RenderPass::RenderPassType::MipChain ||
					renderPass.modifiedShaderId.empty())
				{
					continue;
				}

				for (const auto& shaderTargetBinding : shaderTargetBindings)
				{
					if (shaderTargetBinding.second.type != ShaderTarget::ComputeShader &&
						shaderTargetBinding.second.modifiedShaderId == renderPass.modifiedShaderId)
					{
						return true;
					}
				}
			}
			return false;
		}

		CommandListRenderState& GetCommandListState(ID3D12GraphicsCommandList* commandList)
		{
			if (commandList == gCachedCommandList && gCachedCommandListState)
				return *gCachedCommandListState;

			std::lock_guard<std::mutex> lock(gCommandListRegistryMutex);
			auto& state = gCommandListStates[commandList];
			if (!state)
				state = std::make_unique<CommandListRenderState>();

			gCachedCommandList = commandList;
			gCachedCommandListState = state.get();
			return *gCachedCommandListState;
		}

		uint64_t RootBindingKey(bool computePipeline, uint32_t bindingKind, UINT rootParameterIndex)
		{
			return (static_cast<uint64_t>(computePipeline ? 1 : 0) << 63) |
				(static_cast<uint64_t>(bindingKind) << 32) |
				rootParameterIndex;
		}

		const char* PipelineName(bool computePipeline)
		{
			return computePipeline ? "Compute" : "Graphics";
		}

		void EnsureDescriptorIncrementSizes(
			ID3D12GraphicsCommandList* commandList,
			CommandListRenderState& state)
		{
			if (state.descriptorIncrementSizesInitialized)
				return;

			ID3D12Device* device = nullptr;
			if (SUCCEEDED(commandList->GetDevice(IID_PPV_ARGS(&device))) && device)
			{
				for (UINT heapType = 0; heapType < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++heapType)
				{
					state.descriptorIncrementSizes[heapType] = device->GetDescriptorHandleIncrementSize(
						static_cast<D3D12_DESCRIPTOR_HEAP_TYPE>(heapType));
				}
				device->Release();
				state.descriptorIncrementSizesInitialized = true;
			}
		}

		bool ModifiedShaderMatches(
			const RenderPass::RenderPassDisk& renderPass,
			const ShaderTargetBinding& shaderTarget)
		{
			return !renderPass.modifiedShaderId.empty() &&
				renderPass.modifiedShaderId == shaderTarget.modifiedShaderId;
		}

		bool HasLinkedShaderTargetBinding(
			const RenderPass::RenderPassDisk& renderPass,
			const ShaderTargetBindingMap& shaderTargetBindings)
		{
			for (const auto& shaderTargetBinding : shaderTargetBindings)
			{
				if (ModifiedShaderMatches(renderPass, shaderTargetBinding.second))
					return true;
			}
			return false;
		}

		void RefreshPendingResourceSnapshots(
			const RenderPassConfigurationSnapshot& configuration,
			const ShaderTargetBindingMap& shaderTargetBindings)
		{
			std::lock_guard<std::mutex> diagnosticsLock(gDiagnosticsMutex);
			gPendingResourceSnapshotIds.clear();
			for (const RenderPass::RenderPassDisk& renderPass : configuration.renderPasses)
			{
				if (!renderPass.enabled || !renderPass.trackResourceBindings ||
					!HasLinkedShaderTargetBinding(renderPass, shaderTargetBindings))
				{
					continue;
				}

				const auto diagnosticsIt = gDiagnosticsByRenderPassId.find(renderPass.id);
				const bool currentSnapshotAvailable = diagnosticsIt != gDiagnosticsByRenderPassId.end() &&
					diagnosticsIt->second.resourceSnapshotCaptured &&
					diagnosticsIt->second.lastModifiedShaderId == renderPass.modifiedShaderId;
				if (!currentSnapshotAvailable)
					gPendingResourceSnapshotIds.insert(renderPass.id);
			}

			gResourceTrackingRequired.store(!gPendingResourceSnapshotIds.empty(), std::memory_order_release);
		}

		void RefreshExecutionTrackingFlags(
			const RenderPassConfigurationSnapshot& configuration,
			const ShaderTargetBindingMap& shaderTargetBindings)
		{
			uint32_t graphicsBoundaryMask = 0;
			uint32_t computeBoundaryMask = 0;
			for (const RenderPass::RenderPassDisk& renderPass : configuration.renderPasses)
			{
				if (!renderPass.enabled)
					continue;

				const uint32_t boundaryMask = renderPass.type == RenderPass::RenderPassType::MipChain
					? 1u
					: renderPass.timing == RenderPass::timingAfter ? 2u : 1u;
				for (const auto& shaderTargetBinding : shaderTargetBindings)
				{
					if (!ModifiedShaderMatches(renderPass, shaderTargetBinding.second))
						continue;
					if (renderPass.type == RenderPass::RenderPassType::MipChain &&
						shaderTargetBinding.second.type == ShaderTarget::ComputeShader)
					{
						continue;
					}

					if (shaderTargetBinding.second.type == ShaderTarget::ComputeShader)
						computeBoundaryMask |= boundaryMask;
					else
						graphicsBoundaryMask |= boundaryMask;
				}
			}

			gGraphicsExecutionBoundaryMask.store(graphicsBoundaryMask, std::memory_order_release);
			gComputeExecutionBoundaryMask.store(computeBoundaryMask, std::memory_order_release);
			gExecutionTrackingGeneration.fetch_add(1, std::memory_order_acq_rel);
		}

		void RefreshCommandListExecutionBoundaries(CommandListRenderState& state)
		{
			state.graphicsExecutionBoundaryMask = 0;
			state.computeExecutionBoundaryMask = 0;
			state.executionTrackingGeneration =
				gExecutionTrackingGeneration.load(std::memory_order_acquire);
			if (!state.pipelineState)
				return;

			const ShaderTargetBindingMap* shaderTargetBindings =
				gPublishedShaderTargetBindings.load(std::memory_order_acquire);
			const auto shaderTargetIt = shaderTargetBindings->find(state.pipelineState);
			if (shaderTargetIt == shaderTargetBindings->end())
				return;

			const RenderPassConfigurationSnapshot* configuration =
				gPublishedConfiguration.load(std::memory_order_acquire);
			uint32_t& boundaryMask = shaderTargetIt->second.type == ShaderTarget::ComputeShader
				? state.computeExecutionBoundaryMask
				: state.graphicsExecutionBoundaryMask;
			for (const RenderPass::RenderPassDisk& renderPass : configuration->renderPasses)
			{
				if (!renderPass.enabled || !ModifiedShaderMatches(renderPass, shaderTargetIt->second))
					continue;
				if (renderPass.type == RenderPass::RenderPassType::MipChain &&
					shaderTargetIt->second.type == ShaderTarget::ComputeShader)
				{
					continue;
				}
				boundaryMask |= renderPass.type == RenderPass::RenderPassType::MipChain
					? 1u
					: renderPass.timing == RenderPass::timingAfter ? 2u : 1u;
			}
		}

		const DescriptorHeapState* ResolveDescriptorTableLocation(
			const CommandListRenderState& state,
			RenderPass::ResourceBindingDiagnostic& binding)
		{
			for (const DescriptorHeapState& heap : state.descriptorHeaps)
			{
				if (!heap.gpuStart.ptr || !heap.descriptorIncrementSize || !heap.descriptorCount)
					continue;

				const uint64_t heapStart = heap.gpuStart.ptr;
				const uint64_t heapSize = static_cast<uint64_t>(heap.descriptorIncrementSize) * heap.descriptorCount;
				if (binding.gpuDescriptorHandle < heapStart || binding.gpuDescriptorHandle >= heapStart + heapSize)
					continue;

				const uint64_t byteOffset = binding.gpuDescriptorHandle - heapStart;
				binding.descriptorHeapType = static_cast<uint32_t>(heap.type);
				binding.descriptorIndex = static_cast<uint32_t>(byteOffset / heap.descriptorIncrementSize);
				binding.cpuDescriptorHandle = heap.cpuStart.ptr + byteOffset;
				return &heap;
			}

			return nullptr;
		}

		std::vector<RenderPass::ResourceBindingDiagnostic> BuildResourceSnapshot(
			const CommandListRenderState& state,
			bool computePipeline,
			uint32_t maximumTrackedDescriptors)
		{
			std::vector<RenderPass::ResourceBindingDiagnostic> bindings;
			bindings.reserve(state.descriptorHeaps.size() + state.rootBindings.size());

			for (const DescriptorHeapState& heap : state.descriptorHeaps)
			{
				RenderPass::ResourceBindingDiagnostic binding{};
				binding.pipeline = "Shared";
				binding.bindingType = "Descriptor Heap";
				binding.gpuDescriptorHandle = heap.gpuStart.ptr;
				binding.cpuDescriptorHandle = heap.cpuStart.ptr;
				binding.descriptorHeapType = static_cast<uint32_t>(heap.type);
				binding.descriptorCount = heap.descriptorCount;
				bindings.push_back(std::move(binding));
			}

			const std::string expectedPipeline = PipelineName(computePipeline);
			ID3D12RootSignature* rootSignature = computePipeline
				? state.computeRootSignature
				: state.graphicsRootSignature;
			for (const auto& pair : state.rootBindings)
			{
				if (pair.second.pipeline != expectedPipeline)
					continue;

				RenderPass::ResourceBindingDiagnostic binding = pair.second;
				if (binding.bindingType == "Descriptor Table")
				{
					const DescriptorHeapState* descriptorHeap = ResolveDescriptorTableLocation(state, binding);
					if (descriptorHeap)
					{
						RenderPassResourceRegistry::ResolveDescriptorTable(
							rootSignature,
							binding.rootParameterIndex,
							{ binding.cpuDescriptorHandle },
							binding.descriptorHeapType,
							binding.descriptorIndex,
							descriptorHeap->descriptorIncrementSize,
							maximumTrackedDescriptors,
							expectedPipeline,
							bindings);
					}
				}
				else if (binding.gpuAddress)
				{
					RenderPass::ResourceBindingDiagnostic resolvedResource{};
					if (RenderPassResourceRegistry::ResolveGpuVirtualAddress(binding.gpuAddress, resolvedResource))
					{
						resolvedResource.pipeline = binding.pipeline;
						resolvedResource.bindingType = binding.bindingType;
						resolvedResource.rootParameterIndex = binding.rootParameterIndex;
						binding = std::move(resolvedResource);
					}
					RenderPassResourceRegistry::AnnotateRootDescriptor(
						rootSignature,
						binding.rootParameterIndex,
						binding);
				}
				bindings.push_back(std::move(binding));
			}

			if (!computePipeline)
			{
				for (const RenderPass::ResourceBindingDiagnostic& trackedInput : state.inputBindings)
				{
					RenderPass::ResourceBindingDiagnostic binding = trackedInput;
					RenderPass::ResourceBindingDiagnostic resolvedResource{};
					if (binding.gpuAddress &&
						RenderPassResourceRegistry::ResolveGpuVirtualAddress(binding.gpuAddress, resolvedResource))
					{
						resolvedResource.pipeline = binding.pipeline;
						resolvedResource.bindingType = binding.bindingType;
						resolvedResource.shaderRegister = binding.shaderRegister;
						resolvedResource.gpuAddress = binding.gpuAddress;
						resolvedResource.bufferSize = binding.bufferSize;
						resolvedResource.structureByteStride = binding.structureByteStride;
						resolvedResource.resourceFormat = binding.resourceFormat;
						binding = std::move(resolvedResource);
					}
					bindings.push_back(std::move(binding));
				}

				for (const RenderPass::ResourceBindingDiagnostic& trackedOutput : state.outputBindings)
				{
					RenderPass::ResourceBindingDiagnostic binding = trackedOutput;
					RenderPass::ResourceBindingDiagnostic resolvedResource{};
					if (binding.cpuDescriptorHandle && RenderPassResourceRegistry::ResolveDescriptor(
						{ static_cast<SIZE_T>(binding.cpuDescriptorHandle) },
						resolvedResource))
					{
						resolvedResource.pipeline = binding.pipeline;
						resolvedResource.bindingType = binding.bindingType;
						resolvedResource.cpuDescriptorHandle = binding.cpuDescriptorHandle;
						resolvedResource.descriptorIndex = binding.descriptorIndex;
						binding = std::move(resolvedResource);
					}
					bindings.push_back(std::move(binding));
				}
			}

			std::sort(bindings.begin(), bindings.end(), [](const auto& left, const auto& right)
			{
				if (left.pipeline != right.pipeline)
					return left.pipeline < right.pipeline;
				if (left.rootParameterIndex != right.rootParameterIndex)
					return left.rootParameterIndex < right.rootParameterIndex;
				return left.bindingType < right.bindingType;
			});
			return bindings;
		}

		RenderPassMipChain::GraphicsStateSnapshot BuildMipChainGraphicsState(
			const CommandListRenderState& state)
		{
			RenderPassMipChain::GraphicsStateSnapshot snapshot{};
			snapshot.rootSignature = state.graphicsRootSignature;
			snapshot.pipelineState = state.boundPipelineState;
			snapshot.primitiveTopology = state.primitiveTopology;
			snapshot.viewports = state.viewports;
			snapshot.scissorRectangles = state.scissorRectangles;

			snapshot.descriptorHeaps.reserve(state.descriptorHeaps.size());
			for (const DescriptorHeapState& heap : state.descriptorHeaps)
			{
				snapshot.descriptorHeaps.push_back({
					heap.heap,
					heap.type,
					heap.descriptorCount,
					heap.descriptorIncrementSize,
					heap.cpuStart,
					heap.gpuStart });
			}

			for (const auto& binding : state.rootBindings)
			{
				if (binding.second.pipeline == "Graphics")
					snapshot.rootBindings.push_back(binding.second);
			}
			std::sort(snapshot.rootBindings.begin(), snapshot.rootBindings.end(), [](const auto& left, const auto& right)
			{
				if (left.rootParameterIndex != right.rootParameterIndex)
					return left.rootParameterIndex < right.rootParameterIndex;
				return left.bindingType < right.bindingType;
			});

			UINT renderTargetCount = 0;
			for (const RenderPass::ResourceBindingDiagnostic& binding : state.outputBindings)
			{
				if (binding.bindingType == "RTV" && binding.descriptorIndex != UINT32_MAX)
					renderTargetCount = (std::max)(renderTargetCount, binding.descriptorIndex + 1);
			}
			snapshot.renderTargets.resize(renderTargetCount);
			for (const RenderPass::ResourceBindingDiagnostic& binding : state.outputBindings)
			{
				if (binding.bindingType == "RTV" && binding.descriptorIndex < snapshot.renderTargets.size())
					snapshot.renderTargets[binding.descriptorIndex].ptr = binding.cpuDescriptorHandle;
				else if (binding.bindingType == "DSV")
					snapshot.depthStencil.ptr = binding.cpuDescriptorHandle;
			}
			return snapshot;
		}
	}

	void PublishRenderPassConfigurations(const std::vector<RenderPass::RenderPassDisk>& renderPasses)
	{
		auto snapshot = std::make_unique<RenderPassConfigurationSnapshot>();
		snapshot->renderPasses = renderPasses;
		bool hasEnabledRenderPass = false;
		bool hasEnabledMipChainPass = false;
		std::unordered_set<std::string> activeIds;

		for (const RenderPass::RenderPassDisk& renderPass : renderPasses)
		{
			activeIds.insert(renderPass.id);
			if (renderPass.enabled && !renderPass.modifiedShaderId.empty())
			{
				hasEnabledRenderPass = true;
				hasEnabledMipChainPass = hasEnabledMipChainPass ||
					renderPass.type == RenderPass::RenderPassType::MipChain;
			}
		}

		const RenderPassConfigurationSnapshot* snapshotPointer = snapshot.get();
		{
			std::lock_guard<std::mutex> lock(gConfigurationPublishMutex);
			gOwnedConfigurationSnapshots.push_back(std::move(snapshot));
		}

		gPublishedConfiguration.store(snapshotPointer, std::memory_order_release);
		gHasEnabledRenderPasses.store(hasEnabledRenderPass, std::memory_order_release);
		gHasEnabledMipChainPasses.store(hasEnabledMipChainPass, std::memory_order_release);
		const ShaderTargetBindingMap* shaderTargetBindings =
			gPublishedShaderTargetBindings.load(std::memory_order_acquire);
		gHasExecutableRenderPassBinding.store(
			HasLinkedShaderTargetBinding(*snapshotPointer, *shaderTargetBindings),
			std::memory_order_release);
		gHasExecutableMipChainBinding.store(
			HasLinkedMipChainBinding(*snapshotPointer, *shaderTargetBindings),
			std::memory_order_release);
		RefreshExecutionTrackingFlags(*snapshotPointer, *shaderTargetBindings);

		{
			std::lock_guard<std::mutex> diagnosticsLock(gDiagnosticsMutex);
			for (auto diagnosticsIt = gDiagnosticsByRenderPassId.begin(); diagnosticsIt != gDiagnosticsByRenderPassId.end();)
			{
				if (activeIds.find(diagnosticsIt->first) == activeIds.end())
					diagnosticsIt = gDiagnosticsByRenderPassId.erase(diagnosticsIt);
				else
					++diagnosticsIt;
			}
		}
		RefreshPendingResourceSnapshots(*snapshotPointer, *shaderTargetBindings);
	}

	bool HasEnabledRenderPasses()
	{
		return gHasEnabledRenderPasses.load(std::memory_order_acquire);
	}

	bool HasEnabledMipChainPasses()
	{
		return gHasEnabledMipChainPasses.load(std::memory_order_acquire);
	}

	bool IsTrackingRequired()
	{
		// A configured pass cannot execute until at least one live game PSO has been
		// resolved to a linked shader target. Avoid observing every startup command and
		// resource while the injector still has nothing it could legally inject into.
		return HasEnabledRenderPasses() &&
			gHasExecutableRenderPassBinding.load(std::memory_order_acquire);
	}

	bool IsResourceTrackingRequired()
	{
		return IsTrackingRequired() && gResourceTrackingRequired.load(std::memory_order_acquire);
	}

	bool IsDescriptorRegistryTrackingRequired()
	{
		return IsResourceTrackingRequired() ||
			(IsTrackingRequired() && gHasExecutableMipChainBinding.load(std::memory_order_acquire));
	}

	bool IsGraphicsStateTrackingRequired()
	{
		return IsDescriptorRegistryTrackingRequired();
	}

	bool HasPendingCommandListSubmissionWork()
	{
		return RenderPassMipChain::HasRecordedCommandListWork();
	}

	bool IsExecutionTrackingRequired(bool computePipeline, ExecutionBoundary boundary)
	{
		const uint32_t requiredBoundary = boundary == ExecutionBoundary::After ? 2u : 1u;
		const uint32_t boundaryMask = computePipeline
			? gComputeExecutionBoundaryMask.load(std::memory_order_acquire)
			: gGraphicsExecutionBoundaryMask.load(std::memory_order_acquire);
		return (boundaryMask & requiredBoundary) != 0;
	}

	bool ShouldRecordExecutionBoundary(
		ID3D12GraphicsCommandList* commandList,
		bool computePipeline,
		ExecutionBoundary boundary)
	{
		if (!commandList || !IsExecutionTrackingRequired(computePipeline, boundary))
			return false;

		CommandListRenderState& state = GetCommandListState(commandList);
		const uint64_t currentGeneration =
			gExecutionTrackingGeneration.load(std::memory_order_acquire);
		if (state.executionTrackingGeneration != currentGeneration)
			RefreshCommandListExecutionBoundaries(state);

		const uint32_t requiredBoundary = boundary == ExecutionBoundary::After ? 2u : 1u;
		const uint32_t boundaryMask = computePipeline
			? state.computeExecutionBoundaryMask
			: state.graphicsExecutionBoundaryMask;
		return (boundaryMask & requiredBoundary) != 0;
	}

	void BeginShaderTargetBindingUpdate()
	{
		std::lock_guard<std::mutex> lock(gConfigurationPublishMutex);
		gPendingShaderTargetBindings.clear();
	}

	void AddShaderTargetBinding(
		ID3D12PipelineState* pipelineState,
		const std::string& modifiedShaderId,
		const std::string& shaderTargetName,
		uint64_t shaderTargetHash,
		ShaderTarget::ShaderType shaderTargetType,
		const PipelineOutputState& outputState)
	{
		if (!pipelineState || modifiedShaderId.empty() || shaderTargetName.empty())
			return;

		std::lock_guard<std::mutex> lock(gConfigurationPublishMutex);
		gPendingShaderTargetBindings[pipelineState] = {
			modifiedShaderId,
			shaderTargetName,
			shaderTargetHash,
			shaderTargetType,
			outputState };
	}

	void CommitShaderTargetBindingUpdate()
	{
		std::lock_guard<std::mutex> lock(gConfigurationPublishMutex);
		auto snapshot = std::make_unique<const ShaderTargetBindingMap>(gPendingShaderTargetBindings);
		const ShaderTargetBindingMap* snapshotPointer = snapshot.get();
		gOwnedShaderTargetBindingSnapshots.push_back(std::move(snapshot));
		gPublishedShaderTargetBindings.store(snapshotPointer, std::memory_order_release);
		const RenderPassConfigurationSnapshot* configuration =
			gPublishedConfiguration.load(std::memory_order_acquire);
		gHasExecutableRenderPassBinding.store(
			HasLinkedShaderTargetBinding(*configuration, *snapshotPointer),
			std::memory_order_release);
		gHasExecutableMipChainBinding.store(
			HasLinkedMipChainBinding(*configuration, *snapshotPointer),
			std::memory_order_release);
		RefreshExecutionTrackingFlags(*configuration, *snapshotPointer);
		RefreshPendingResourceSnapshots(*configuration, *snapshotPointer);
	}

	void ResetCommandList(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* initialPipelineState)
	{
		if (!IsTrackingRequired())
			return;

		CommandListRenderState& state = GetCommandListState(commandList);
		state.pipelineState = initialPipelineState;
		state.boundPipelineState = initialPipelineState;
		state.graphicsRootSignature = nullptr;
		state.computeRootSignature = nullptr;
		state.primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
		state.descriptorHeaps.clear();
		state.rootBindings.clear();
		state.inputBindings.clear();
		state.outputBindings.clear();
		state.viewports.clear();
		state.scissorRectangles.clear();
		RefreshCommandListExecutionBoundaries(state);
	}

	void CompleteCommandListReset(ID3D12GraphicsCommandList* commandList, bool resetSucceeded)
	{
		if (resetSucceeded)
			RenderPassMipChain::ResetCommandListRecording(commandList);
	}

	void TrackPipelineState(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* pipelineState)
	{
		if (!IsTrackingRequired())
			return;

		CommandListRenderState& state = GetCommandListState(commandList);
		state.pipelineState = pipelineState;
		state.boundPipelineState = pipelineState;
		RefreshCommandListExecutionBoundaries(state);
	}

	void TrackBoundPipelineState(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* pipelineState)
	{
		if (IsTrackingRequired())
			GetCommandListState(commandList).boundPipelineState = pipelineState;
	}

	void TrackPrimitiveTopology(
		ID3D12GraphicsCommandList* commandList,
		D3D12_PRIMITIVE_TOPOLOGY primitiveTopology)
	{
		if (IsTrackingRequired())
			GetCommandListState(commandList).primitiveTopology = primitiveTopology;
	}

	void TrackRootSignature(
		ID3D12GraphicsCommandList* commandList,
		bool computePipeline,
		ID3D12RootSignature* rootSignature)
	{
		if (!IsTrackingRequired())
			return;

		CommandListRenderState& state = GetCommandListState(commandList);
		ID3D12RootSignature*& currentRootSignature = computePipeline
			? state.computeRootSignature
			: state.graphicsRootSignature;
		if (currentRootSignature == rootSignature)
			return;

		currentRootSignature = rootSignature;
		if (!computePipeline)
			HookD3D12::EnsureRenderPassRootSignatureRegistered(rootSignature);
		for (auto bindingIt = state.rootBindings.begin(); bindingIt != state.rootBindings.end();)
		{
			const bool bindingIsCompute = (bindingIt->first & (uint64_t{ 1 } << 63)) != 0;
			if (bindingIsCompute == computePipeline)
				bindingIt = state.rootBindings.erase(bindingIt);
			else
				++bindingIt;
		}
	}

	void TrackDescriptorHeaps(
		ID3D12GraphicsCommandList* commandList,
		UINT descriptorHeapCount,
		ID3D12DescriptorHeap* const* descriptorHeaps)
	{
		if (!IsTrackingRequired())
			return;

		CommandListRenderState& state = GetCommandListState(commandList);
		state.descriptorHeaps.clear();
		state.descriptorHeaps.reserve(descriptorHeapCount);
		for (auto bindingIt = state.rootBindings.begin(); bindingIt != state.rootBindings.end();)
		{
			if (bindingIt->second.bindingType == "Descriptor Table")
				bindingIt = state.rootBindings.erase(bindingIt);
			else
				++bindingIt;
		}

		EnsureDescriptorIncrementSizes(commandList, state);

		for (UINT heapIndex = 0; heapIndex < descriptorHeapCount; ++heapIndex)
		{
			ID3D12DescriptorHeap* descriptorHeap = descriptorHeaps ? descriptorHeaps[heapIndex] : nullptr;
			if (!descriptorHeap)
				continue;

			DescriptorHeapState heap{};
			heap.heap = descriptorHeap;
			const D3D12_DESCRIPTOR_HEAP_DESC description = descriptorHeap->GetDesc();
			heap.type = description.Type;
			heap.descriptorCount = description.NumDescriptors;
			if (description.Type < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES)
				heap.descriptorIncrementSize = state.descriptorIncrementSizes[description.Type];
			heap.cpuStart = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
			if ((description.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0)
				heap.gpuStart = descriptorHeap->GetGPUDescriptorHandleForHeapStart();

			state.descriptorHeaps.push_back(heap);
		}
	}

	void TrackRootDescriptorTable(
		ID3D12GraphicsCommandList* commandList,
		bool computePipeline,
		UINT rootParameterIndex,
		D3D12_GPU_DESCRIPTOR_HANDLE descriptorHandle)
	{
		if (!IsTrackingRequired())
			return;

		CommandListRenderState& state = GetCommandListState(commandList);
		RenderPass::ResourceBindingDiagnostic binding{};
		binding.pipeline = PipelineName(computePipeline);
		binding.bindingType = "Descriptor Table";
		binding.rootParameterIndex = rootParameterIndex;
		binding.gpuDescriptorHandle = descriptorHandle.ptr;
		state.rootBindings[RootBindingKey(computePipeline, 1, rootParameterIndex)] = std::move(binding);
	}

	void TrackRootDescriptor(
		ID3D12GraphicsCommandList* commandList,
		bool computePipeline,
		const char* bindingType,
		UINT rootParameterIndex,
		D3D12_GPU_VIRTUAL_ADDRESS gpuAddress)
	{
		if (!IsTrackingRequired())
			return;

		uint32_t bindingKind = 2;
		if (bindingType && bindingType[0] == 'S')
			bindingKind = 3;
		else if (bindingType && bindingType[0] == 'U')
			bindingKind = 4;

		RenderPass::ResourceBindingDiagnostic binding{};
		binding.pipeline = PipelineName(computePipeline);
		binding.bindingType = bindingType ? bindingType : "Root Descriptor";
		binding.rootParameterIndex = rootParameterIndex;
		binding.gpuAddress = gpuAddress;
		GetCommandListState(commandList).rootBindings[RootBindingKey(computePipeline, bindingKind, rootParameterIndex)] = std::move(binding);
	}

	void TrackRootConstants(
		ID3D12GraphicsCommandList* commandList,
		bool computePipeline,
		UINT rootParameterIndex,
		UINT valueCount,
		const void* values,
		UINT destinationOffset)
	{
		if (!IsTrackingRequired())
			return;

		CommandListRenderState& state = GetCommandListState(commandList);
		RenderPass::ResourceBindingDiagnostic& binding =
			state.rootBindings[RootBindingKey(computePipeline, 5, rootParameterIndex)];
		binding.pipeline = PipelineName(computePipeline);
		binding.bindingType = "Root Constants";
		binding.rootParameterIndex = rootParameterIndex;
		binding.destinationOffset = 0;

		if (!values)
			return;

		if (destinationOffset > UINT_MAX - valueCount)
			return;
		const UINT capturedValueCount = valueCount;
		const size_t requiredSize = destinationOffset + capturedValueCount;
		if (binding.rootConstants.size() < requiredSize)
			binding.rootConstants.resize(requiredSize);

		const uint32_t* sourceValues = static_cast<const uint32_t*>(values);
		std::copy(
			sourceValues,
			sourceValues + capturedValueCount,
			binding.rootConstants.begin() + destinationOffset);
	}

	void TrackIndexBuffer(
		ID3D12GraphicsCommandList* commandList,
		const D3D12_INDEX_BUFFER_VIEW* view)
	{
		if (!IsTrackingRequired())
			return;

		CommandListRenderState& state = GetCommandListState(commandList);
		state.inputBindings.erase(
			std::remove_if(state.inputBindings.begin(), state.inputBindings.end(), [](const auto& binding)
			{
				return binding.bindingType == "Index Buffer";
			}),
			state.inputBindings.end());
		if (!view || !view->BufferLocation)
			return;

		RenderPass::ResourceBindingDiagnostic binding{};
		binding.pipeline = "Graphics";
		binding.bindingType = "Index Buffer";
		binding.gpuAddress = view->BufferLocation;
		binding.bufferSize = view->SizeInBytes;
		binding.resourceFormat = static_cast<uint32_t>(view->Format);
		state.inputBindings.push_back(std::move(binding));
	}

	void TrackVertexBuffers(
		ID3D12GraphicsCommandList* commandList,
		UINT startSlot,
		UINT viewCount,
		const D3D12_VERTEX_BUFFER_VIEW* views)
	{
		if (!IsTrackingRequired())
			return;

		CommandListRenderState& state = GetCommandListState(commandList);
		const uint64_t endSlot = static_cast<uint64_t>(startSlot) + viewCount;
		state.inputBindings.erase(
			std::remove_if(state.inputBindings.begin(), state.inputBindings.end(), [startSlot, endSlot](const auto& binding)
			{
				return binding.bindingType == "Vertex Buffer" &&
					binding.shaderRegister >= startSlot &&
					binding.shaderRegister < endSlot;
			}),
			state.inputBindings.end());

		if (!views)
			return;
		for (UINT viewIndex = 0; viewIndex < viewCount; ++viewIndex)
		{
			const D3D12_VERTEX_BUFFER_VIEW& view = views[viewIndex];
			if (!view.BufferLocation)
				continue;

			RenderPass::ResourceBindingDiagnostic binding{};
			binding.pipeline = "Graphics";
			binding.bindingType = "Vertex Buffer";
			binding.shaderRegister = startSlot + viewIndex;
			binding.gpuAddress = view.BufferLocation;
			binding.bufferSize = view.SizeInBytes;
			binding.structureByteStride = view.StrideInBytes;
			state.inputBindings.push_back(std::move(binding));
		}
	}

	void TrackRenderTargets(
		ID3D12GraphicsCommandList* commandList,
		UINT renderTargetCount,
		const D3D12_CPU_DESCRIPTOR_HANDLE* renderTargetDescriptors,
		BOOL descriptorsAreContiguous,
		const D3D12_CPU_DESCRIPTOR_HANDLE* depthStencilDescriptor)
	{
		if (!IsTrackingRequired())
			return;

		CommandListRenderState& state = GetCommandListState(commandList);
		EnsureDescriptorIncrementSizes(commandList, state);
		state.outputBindings.clear();

		const UINT renderTargetIncrement = state.descriptorIncrementSizes[D3D12_DESCRIPTOR_HEAP_TYPE_RTV];
		for (UINT renderTargetIndex = 0;
			renderTargetDescriptors && renderTargetIndex < renderTargetCount;
			++renderTargetIndex)
		{
			D3D12_CPU_DESCRIPTOR_HANDLE descriptor = descriptorsAreContiguous
				? D3D12_CPU_DESCRIPTOR_HANDLE{
					renderTargetDescriptors[0].ptr + static_cast<SIZE_T>(renderTargetIndex) * renderTargetIncrement }
				: renderTargetDescriptors[renderTargetIndex];
			RenderPass::ResourceBindingDiagnostic binding{};
			binding.pipeline = "Graphics";
			binding.bindingType = "RTV";
			binding.cpuDescriptorHandle = descriptor.ptr;
			binding.descriptorIndex = renderTargetIndex;
			state.outputBindings.push_back(std::move(binding));
		}

		if (depthStencilDescriptor && depthStencilDescriptor->ptr)
		{
			RenderPass::ResourceBindingDiagnostic binding{};
			binding.pipeline = "Graphics";
			binding.bindingType = "DSV";
			binding.cpuDescriptorHandle = depthStencilDescriptor->ptr;
			state.outputBindings.push_back(std::move(binding));
		}
	}

	void TrackViewports(
		ID3D12GraphicsCommandList* commandList,
		UINT viewportCount,
		const D3D12_VIEWPORT* viewports)
	{
		if (!IsTrackingRequired())
			return;

		CommandListRenderState& state = GetCommandListState(commandList);
		state.viewports.clear();
		if (viewports && viewportCount)
			state.viewports.assign(viewports, viewports + viewportCount);
	}

	void TrackScissorRectangles(
		ID3D12GraphicsCommandList* commandList,
		UINT rectangleCount,
		const D3D12_RECT* rectangles)
	{
		if (!IsTrackingRequired())
			return;

		CommandListRenderState& state = GetCommandListState(commandList);
		state.scissorRectangles.clear();
		if (rectangles && rectangleCount)
			state.scissorRectangles.assign(rectangles, rectangles + rectangleCount);
	}

	void RecordExecutionBoundary(
		ID3D12GraphicsCommandList* commandList,
		bool computePipeline,
		ExecutionBoundary boundary,
		const char* operationName)
	{
		if (!IsTrackingRequired())
			return;

		CommandListRenderState& state = GetCommandListState(commandList);
		if (!state.pipelineState)
			return;

		const ShaderTargetBindingMap* shaderTargetBindings =
			gPublishedShaderTargetBindings.load(std::memory_order_acquire);
		const auto targetIt = shaderTargetBindings->find(state.pipelineState);
		if (targetIt == shaderTargetBindings->end())
			return;

		const char* timing = boundary == ExecutionBoundary::Before ? RenderPass::timingBefore : RenderPass::timingAfter;
		const RenderPassConfigurationSnapshot* configuration =
			gPublishedConfiguration.load(std::memory_order_acquire);
		std::vector<RenderPassMipChain::ExecutionResult> mipChainResults;
		if (!computePipeline && boundary == ExecutionBoundary::Before)
		{
			std::vector<const RenderPass::RenderPassDisk*> matchingMipChains;
			for (const RenderPass::RenderPassDisk& renderPass : configuration->renderPasses)
			{
				if (renderPass.enabled && renderPass.type == RenderPass::RenderPassType::MipChain &&
					ModifiedShaderMatches(renderPass, targetIt->second))
				{
					matchingMipChains.push_back(&renderPass);
				}
			}
			if (!matchingMipChains.empty())
			{
				mipChainResults = RenderPassMipChain::PrepareForTargetDraw(
					matchingMipChains,
					commandList,
					BuildMipChainGraphicsState(state));
			}
		}

		for (const RenderPass::RenderPassDisk& renderPass : configuration->renderPasses)
		{
			const bool matchesBoundary = renderPass.type == RenderPass::RenderPassType::MipChain
				? boundary == ExecutionBoundary::Before
				: renderPass.timing == timing;
			if (!renderPass.enabled || !matchesBoundary || !ModifiedShaderMatches(renderPass, targetIt->second) ||
				(renderPass.type == RenderPass::RenderPassType::MipChain && computePipeline))
				continue;

			bool executionAttempted = false;
			bool executionSucceeded = false;
			std::string executionError;
			if (renderPass.type == RenderPass::RenderPassType::MipChain)
			{
				const auto resultIt = std::find_if(mipChainResults.begin(), mipChainResults.end(), [&](const auto& result)
				{
					return result.renderPassId == renderPass.id;
				});
				if (resultIt != mipChainResults.end())
				{
					executionAttempted = resultIt->attempted;
					executionSucceeded = resultIt->succeeded;
					executionError = resultIt->error;
				}
			}
			else if (!computePipeline && RenderPass::HasCompiledShaders(renderPass))
			{
				std::vector<RenderPass::ResourceBindingDiagnostic> effectiveOutputBindings = state.outputBindings;
				const PipelineOutputState& pipelineOutputs = targetIt->second.outputState;
				for (UINT renderTargetIndex = 0;
					renderTargetIndex < pipelineOutputs.renderTargetCount &&
					renderTargetIndex < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
					++renderTargetIndex)
				{
					auto outputIt = std::find_if(
						effectiveOutputBindings.begin(),
						effectiveOutputBindings.end(),
						[renderTargetIndex](const RenderPass::ResourceBindingDiagnostic& binding)
						{
							return binding.bindingType == "RTV" && binding.descriptorIndex == renderTargetIndex;
						});

					if (outputIt == effectiveOutputBindings.end())
					{
						RenderPass::ResourceBindingDiagnostic output{};
						output.pipeline = "Graphics";
						output.bindingType = "RTV";
						output.descriptorIndex = renderTargetIndex;
						effectiveOutputBindings.push_back(std::move(output));
						outputIt = std::prev(effectiveOutputBindings.end());
					}

					if (outputIt->resourceFormat == DXGI_FORMAT_UNKNOWN)
						outputIt->resourceFormat = static_cast<uint32_t>(pipelineOutputs.renderTargetFormats[renderTargetIndex]);
					if (!outputIt->resourceSampleCount)
						outputIt->resourceSampleCount = pipelineOutputs.sampleCount;
					outputIt->resourceSampleQuality = pipelineOutputs.sampleQuality;
				}

				executionAttempted = true;
				executionSucceeded = RenderPassExecutor::ExecuteFullscreenTriangle(
					renderPass,
					commandList,
					state.graphicsRootSignature,
					state.boundPipelineState,
					state.primitiveTopology,
					effectiveOutputBindings,
					executionError);
			}

			bool firstTrigger = false;
			bool firstSuccessfulExecution = false;
			bool firstFailedExecution = false;
			bool captureResourceSnapshot = false;
			{
				std::lock_guard<std::mutex> lock(gDiagnosticsMutex);
				RenderPass::RuntimeDiagnostics& diagnostics = gDiagnosticsByRenderPassId[renderPass.id];
				firstTrigger = diagnostics.triggerCount == 0;
				++diagnostics.triggerCount;
				if (executionSucceeded)
				{
					firstSuccessfulExecution = diagnostics.executionCount == 0;
					++diagnostics.executionCount;
				}
				else if (executionAttempted)
				{
					firstFailedExecution = diagnostics.executionFailureCount == 0;
					++diagnostics.executionFailureCount;
				}
				const bool targetChanged = diagnostics.lastModifiedShaderId != targetIt->second.modifiedShaderId ||
					diagnostics.lastShaderTargetName != targetIt->second.name;
				const bool executionStateChanged =
					diagnostics.lastExecutionError.empty() != executionError.empty();
				if (firstTrigger || targetChanged || executionStateChanged)
				{
					diagnostics.lastTiming = timing;
					diagnostics.lastOperation = operationName ? operationName : "Unknown";
					diagnostics.lastExecutionError = executionError;
					diagnostics.lastModifiedShaderId = targetIt->second.modifiedShaderId;
					diagnostics.lastShaderTargetName = targetIt->second.name;
					char hashText[32]{};
					sprintf_s(hashText, "%016llX", static_cast<unsigned long long>(targetIt->second.hash));
					diagnostics.lastShaderTargetHash = hashText;
				}

				if (renderPass.trackResourceBindings)
				{
					const auto pendingSnapshotIt = gPendingResourceSnapshotIds.find(renderPass.id);
					if (pendingSnapshotIt != gPendingResourceSnapshotIds.end())
					{
						captureResourceSnapshot = true;
						gPendingResourceSnapshotIds.erase(pendingSnapshotIt);
						gResourceTrackingRequired.store(
							!gPendingResourceSnapshotIds.empty(),
							std::memory_order_release);
					}
				}
				else
				{
					diagnostics.resourceSnapshotCaptured = false;
					diagnostics.resourceBindings.clear();
				}
			}

			if (captureResourceSnapshot)
			{
				std::vector<RenderPass::ResourceBindingDiagnostic> resourceSnapshot = BuildResourceSnapshot(
					state,
					computePipeline,
					renderPass.maximumTrackedDescriptors);
				std::lock_guard<std::mutex> lock(gDiagnosticsMutex);
				RenderPass::RuntimeDiagnostics& diagnostics = gDiagnosticsByRenderPassId[renderPass.id];
				diagnostics.resourceBindings = std::move(resourceSnapshot);
				diagnostics.resourceSnapshotCaptured = true;
			}

			if (firstTrigger)
			{
				ShaderInjectorIO::WriteToLogFile(StringHelper::Format(
					"RenderPassRuntime->RecordExecutionBoundary: matched pass=%s type=%s operation=%s timing=%s commandList=%p requestedPSO=%p boundPSO=%p rootSignature=%p outputs=%llu pipelineRTVs=%u format0=%u samples=%u/%u compiled=%u mipSource=t%u,space%u",
					renderPass.name.c_str(),
					RenderPass::TypeName(renderPass.type),
					operationName ? operationName : "Unknown",
					timing,
					commandList,
					state.pipelineState,
					state.boundPipelineState,
					state.graphicsRootSignature,
					static_cast<unsigned long long>(state.outputBindings.size()),
					targetIt->second.outputState.renderTargetCount,
					static_cast<UINT>(targetIt->second.outputState.renderTargetFormats[0]),
					targetIt->second.outputState.sampleCount,
					targetIt->second.outputState.sampleQuality,
					RenderPass::HasCompiledShaders(renderPass) ? 1u : 0u,
					renderPass.sourceTextureShaderRegister,
					renderPass.sourceTextureRegisterSpace));
			}

			if (firstSuccessfulExecution)
			{
				ShaderInjectorIO::WriteToLogFileSuccess(
					"RenderPassRuntime->RecordExecutionBoundary: first " +
					std::string(RenderPass::TypeName(renderPass.type)) + " execution succeeded for " +
					renderPass.name + " via " + (operationName ? operationName : "Unknown"));
			}
			else if (firstFailedExecution)
			{
				ShaderInjectorIO::WriteToLogFileError(
					"RenderPassRuntime->RecordExecutionBoundary: first fullscreen execution failed for " +
					renderPass.name + " via " + (operationName ? operationName : "Unknown") +
					" error=" + executionError);
			}
		}
	}

	void CompleteGraphicsExecutionBoundary(ID3D12GraphicsCommandList* commandList)
	{
		RenderPassMipChain::RestoreAfterTargetDraw(commandList);
	}

	void NotifyCommandListsSubmitted(
		ID3D12CommandQueue* commandQueue,
		UINT commandListCount,
		ID3D12CommandList* const* commandLists)
	{
		RenderPassMipChain::NotifyCommandListsSubmitted(
			commandQueue,
			commandListCount,
			commandLists);
	}

	RenderPass::RuntimeDiagnostics GetDiagnostics(const std::string& renderPassId)
	{
		std::lock_guard<std::mutex> lock(gDiagnosticsMutex);
		const auto diagnosticsIt = gDiagnosticsByRenderPassId.find(renderPassId);
		return diagnosticsIt != gDiagnosticsByRenderPassId.end()
			? diagnosticsIt->second
			: RenderPass::RuntimeDiagnostics{};
	}

	void ClearDiagnostics(const std::string& renderPassId)
	{
		const RenderPassConfigurationSnapshot* configuration =
			gPublishedConfiguration.load(std::memory_order_acquire);
		const ShaderTargetBindingMap* shaderTargetBindings =
			gPublishedShaderTargetBindings.load(std::memory_order_acquire);

		std::lock_guard<std::mutex> lock(gDiagnosticsMutex);
		gDiagnosticsByRenderPassId.erase(renderPassId);
		for (const RenderPass::RenderPassDisk& renderPass : configuration->renderPasses)
		{
			if (renderPass.id == renderPassId && renderPass.enabled && renderPass.trackResourceBindings &&
				HasLinkedShaderTargetBinding(renderPass, *shaderTargetBindings))
			{
				gPendingResourceSnapshotIds.insert(renderPassId);
				break;
			}
		}
		gResourceTrackingRequired.store(!gPendingResourceSnapshotIds.empty(), std::memory_order_release);
	}
}
