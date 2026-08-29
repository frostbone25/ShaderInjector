#include "RenderPass/RenderPassRuntime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "HookD3D12.h"
#include "HookD3D12/HookD3D12RenderPass.h"
#include "GUI/ShaderInjectorGUI.h"
#include "Globals.h"
#include "Performance/PerformanceMetrics.h"
#include "RenderPass/RenderPassResourceRegistry.h"
#include "RenderPass/RenderPassExecutor.h"
#include "RenderPass/RenderPassGraph.h"
#include "RenderPass/RenderPassMipChain.h"
#include "RenderPass/RenderPassReplacement.h"
#include "RenderPass/RenderPassTexturePool.h"
#include "ShaderResource/ShaderResourceCatalog.h"
#include "ShaderResource/ShaderResourceRuntime.h"
#include "IO/ShaderInjectorIO.h"
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

		struct ResolvedEventBinding
		{
			bool valid = false;
			std::string modifiedShaderId;
			ExecutionBoundary rootBoundary = ExecutionBoundary::Before;
		};

		struct ModifiedShaderExecutionPlan
		{
			std::array<std::vector<const RenderPass::RenderPassDisk*>, 2> executionOrders;
			std::array<std::vector<const RenderPass::RenderPassDisk*>, 2> mipChainOrders;
			uint32_t graphicsBoundaryMask = 0;
			uint32_t computeBoundaryMask = 0;
			bool hasRuntimeResources = false;
		};

		struct RuntimeCounters
		{
			std::atomic<uint64_t> triggerCount = 0;
			std::atomic<uint64_t> executionCount = 0;
			std::atomic<uint64_t> executionFailureCount = 0;
			std::atomic<uint64_t> reportedTriggerCount = 0;
			std::atomic<uint64_t> reportedExecutionCount = 0;
			std::atomic<uint64_t> reportedFailureCount = 0;
			// 0 = no attempted execution, 1 = success, 2 = failure.
			std::atomic<uint8_t> lastExecutionResult = 0;
		};

		struct RenderPassConfigurationSnapshot
		{
			std::vector<RenderPass::RenderPassDisk> renderPasses;
			std::vector<std::unique_ptr<RuntimeCounters>> runtimeCounters;
			RenderPassGraph::Compilation compiledGraph;
			std::unordered_map<std::string, size_t> renderPassIndices;
			std::vector<ResolvedEventBinding> resolvedEvents;
			std::unordered_map<std::string, ModifiedShaderExecutionPlan> executionPlans;
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

		enum class RootBindingType : uint8_t
		{
			None,
			DescriptorTable,
			ConstantBufferView,
			ShaderResourceView,
			UnorderedAccessView,
			Constants
		};

		struct RootBindingState
		{
			RootBindingType type = RootBindingType::None;
			uint64_t value = 0;
			std::vector<uint32_t> constants;
		};

		struct CommandListRenderState
		{
			CommandListRenderState()
			{
				descriptorHeaps.reserve(2);
				graphicsRootBindings.reserve(24);
				computeRootBindings.reserve(24);
				outputBindings.reserve(D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT + 1);
				viewports.reserve(D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
				scissorRectangles.reserve(D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
			}

			ID3D12PipelineState* pipelineState = nullptr;
			ID3D12PipelineState* boundPipelineState = nullptr;
			ID3D12RootSignature* graphicsRootSignature = nullptr;
			ID3D12RootSignature* computeRootSignature = nullptr;
			D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
			std::vector<DescriptorHeapState> descriptorHeaps;
			std::vector<RootBindingState> graphicsRootBindings;
			std::vector<RootBindingState> computeRootBindings;
			std::vector<RenderPass::ResourceBindingDiagnostic> inputBindings;
			std::vector<RenderPass::ResourceBindingDiagnostic> outputBindings;
			std::vector<D3D12_VIEWPORT> viewports;
			std::vector<D3D12_RECT> scissorRectangles;
			UINT descriptorIncrementSizes[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES]{};
			bool descriptorIncrementSizesInitialized = false;
			uint32_t graphicsExecutionBoundaryMask = 0;
			uint32_t computeExecutionBoundaryMask = 0;
			uint64_t executionTrackingGeneration = 0;
			bool replacementPassActive = false;
		};

		const RenderPassConfigurationSnapshot gEmptyConfigurationSnapshot;
		const ShaderTargetBindingMap gEmptyShaderTargetBindingMap;
		std::atomic<const RenderPassConfigurationSnapshot*> gPublishedConfiguration = &gEmptyConfigurationSnapshot;
		std::atomic<const ShaderTargetBindingMap*> gPublishedShaderTargetBindings = &gEmptyShaderTargetBindingMap;
		std::atomic<bool> gHasEnabledRenderPasses = false;
		std::atomic<bool> gHasEnabledMipChainPasses = false;
		std::atomic<bool> gHasExecutableRenderPassBinding = false;
		std::atomic<bool> gHasExecutableMipChainBinding = false;
		std::atomic<bool> gHasShaderResourcePasses = false;
		std::atomic<bool> gHasInheritedGameBindings = false;
		std::atomic<bool> gHasCustomFullscreenPasses = false;
		std::atomic<bool> gHasGameTextureInputs = false;
		std::atomic<bool> gResourceTrackingRequired = false;
		std::atomic<uint32_t> gTrackingModeFlags = 0;
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
		thread_local RenderPassMipChain::GraphicsStateSnapshot gMipChainGraphicsState;
		thread_local RenderPassMipChain::GraphicsStateSnapshot gFullscreenGraphicsState;
		thread_local RenderPassMipChain::GraphicsStateSnapshot gShaderResourceState;
		thread_local RenderPassMipChain::GraphicsStateSnapshot gOppositeShaderResourceState;
		thread_local ID3D12GraphicsCommandList* gPendingMipRestoreCommandList = nullptr;
		thread_local bool gPendingMipRestore = false;
		struct ThreadGameTextureBindingLookup
		{
			const RenderPass::RenderPassDisk* renderPass = nullptr;
			ID3D12RootSignature* rootSignature = nullptr;
			RenderPass::GameResourceViewType viewType = RenderPass::GameResourceViewType::UnorderedAccess;
			uint32_t shaderRegister = 0;
			uint32_t registerSpace = 0;
			std::vector<RenderPassResourceRegistry::DescriptorBindingLocation> locations;
		};
		thread_local std::array<ThreadGameTextureBindingLookup, 16> gGameTextureBindingLookups;
		thread_local size_t gNextGameTextureBindingLookup = 0;

		std::mutex gDiagnosticsMutex;
		std::unordered_map<std::string, RenderPass::RuntimeDiagnostics> gDiagnosticsByRenderPassId;
		std::unordered_set<std::string> gPendingResourceSnapshotIds;

		enum TrackingModeFlag : uint32_t
		{
			TrackingEnabled = 1u << 0,
			ResourceTrackingEnabled = 1u << 1,
			DescriptorRegistryTrackingEnabled = 1u << 2,
			GraphicsStateTrackingEnabled = 1u << 3,
			DescriptorTableTrackingEnabled = 1u << 4,
			RootBindingTrackingEnabled = 1u << 5
		};

		void RefreshTrackingModeFlags()
		{
			const bool trackingEnabled =
				gHasEnabledRenderPasses.load(std::memory_order_relaxed) &&
				gHasExecutableRenderPassBinding.load(std::memory_order_relaxed);
			const bool resourceTrackingEnabled = trackingEnabled &&
				gResourceTrackingRequired.load(std::memory_order_relaxed);
			const bool rootBindingTrackingEnabled = trackingEnabled &&
				(gHasInheritedGameBindings.load(std::memory_order_relaxed) ||
					gHasExecutableMipChainBinding.load(std::memory_order_relaxed));
			const bool descriptorTableTrackingEnabled = trackingEnabled &&
				(gHasShaderResourcePasses.load(std::memory_order_relaxed) ||
				gHasExecutableMipChainBinding.load(std::memory_order_relaxed) ||
				rootBindingTrackingEnabled ||
				resourceTrackingEnabled);
			// SRVs and their CPU staging copies can be created before shader-target
			// discovery resolves a pass. Preserve that provenance from pass load time;
			// command-list state tracking still waits for an executable target binding.
			const bool descriptorRegistryTrackingEnabled = resourceTrackingEnabled ||
				gHasGameTextureInputs.load(std::memory_order_relaxed) ||
				gHasEnabledMipChainPasses.load(std::memory_order_relaxed) ||
				gHasShaderResourcePasses.load(std::memory_order_relaxed);
			const bool graphicsStateTrackingEnabled = trackingEnabled &&
				(resourceTrackingEnabled ||
				gHasExecutableMipChainBinding.load(std::memory_order_relaxed) ||
				gHasCustomFullscreenPasses.load(std::memory_order_relaxed));

			uint32_t flags = trackingEnabled ? TrackingEnabled : 0;
			if (resourceTrackingEnabled)
				flags |= ResourceTrackingEnabled;
			if (descriptorTableTrackingEnabled)
				flags |= DescriptorTableTrackingEnabled;
			if (rootBindingTrackingEnabled)
				flags |= RootBindingTrackingEnabled;
			if (descriptorRegistryTrackingEnabled)
				flags |= DescriptorRegistryTrackingEnabled;
			if (graphicsStateTrackingEnabled)
				flags |= GraphicsStateTrackingEnabled;
			gTrackingModeFlags.store(flags, std::memory_order_release);
		}

		void BuildResolvedEventBindings(RenderPassConfigurationSnapshot& configuration)
		{
			configuration.compiledGraph = RenderPassGraph::Compile(configuration.renderPasses);
			configuration.renderPassIndices = configuration.compiledGraph.renderPassIndices;
			configuration.resolvedEvents.assign(configuration.renderPasses.size(), {});
			for (const RenderPassGraph::CompiledNode& node : configuration.compiledGraph.nodes)
			{
				if (!node.valid || node.renderPassIndex >= configuration.resolvedEvents.size())
					continue;
				ResolvedEventBinding& resolved = configuration.resolvedEvents[node.renderPassIndex];
				resolved.valid = true;
				resolved.modifiedShaderId = node.modifiedShaderId;
				resolved.rootBoundary = node.rootBoundary == RenderPassGraph::Boundary::After
					? ExecutionBoundary::After
					: ExecutionBoundary::Before;
			}
		}

		const ResolvedEventBinding* FindResolvedEventBinding(
			const RenderPassConfigurationSnapshot& configuration,
			const RenderPass::RenderPassDisk& renderPass)
		{
			const auto renderPassIt = configuration.renderPassIndices.find(renderPass.id);
			if (renderPassIt == configuration.renderPassIndices.end() ||
				renderPassIt->second >= configuration.resolvedEvents.size())
			{
				return nullptr;
			}

			const ResolvedEventBinding& resolved = configuration.resolvedEvents[renderPassIt->second];
			return resolved.valid ? &resolved : nullptr;
		}

		bool HasLinkedShaderTargetBinding(
			const RenderPassConfigurationSnapshot& configuration,
			const ShaderTargetBindingMap& shaderTargetBindings)
		{
			for (const RenderPass::RenderPassDisk& renderPass : configuration.renderPasses)
			{
				const ResolvedEventBinding* resolvedEvent =
					FindResolvedEventBinding(configuration, renderPass);
				if (!resolvedEvent)
					continue;

				for (const auto& shaderTargetBinding : shaderTargetBindings)
				{
					if (shaderTargetBinding.second.modifiedShaderId == resolvedEvent->modifiedShaderId)
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
				const ResolvedEventBinding* resolvedEvent =
					FindResolvedEventBinding(configuration, renderPass);
				if (!resolvedEvent || renderPass.type != RenderPass::RenderPassType::MipChain)
				{
					continue;
				}

				for (const auto& shaderTargetBinding : shaderTargetBindings)
				{
					if (shaderTargetBinding.second.type != ShaderTarget::ComputeShader &&
						shaderTargetBinding.second.modifiedShaderId == resolvedEvent->modifiedShaderId)
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

		std::vector<RootBindingState>& RootBindings(
			CommandListRenderState& state,
			bool computePipeline)
		{
			return computePipeline ? state.computeRootBindings : state.graphicsRootBindings;
		}

		const std::vector<RootBindingState>& RootBindings(
			const CommandListRenderState& state,
			bool computePipeline)
		{
			return computePipeline ? state.computeRootBindings : state.graphicsRootBindings;
		}

		RootBindingState& RootBindingAt(
			CommandListRenderState& state,
			bool computePipeline,
			UINT rootParameterIndex)
		{
			std::vector<RootBindingState>& bindings = RootBindings(state, computePipeline);
			if (bindings.size() <= rootParameterIndex)
				bindings.resize(static_cast<size_t>(rootParameterIndex) + 1);
			return bindings[rootParameterIndex];
		}

		void ResetRootBindings(std::vector<RootBindingState>& bindings, bool descriptorTablesOnly = false)
		{
			for (RootBindingState& binding : bindings)
			{
				if (descriptorTablesOnly && binding.type != RootBindingType::DescriptorTable)
					continue;
				binding.type = RootBindingType::None;
				binding.value = 0;
				binding.constants.clear();
			}
		}

		const char* RootBindingTypeName(RootBindingType type)
		{
			switch (type)
			{
				case RootBindingType::DescriptorTable: return "Descriptor Table";
				case RootBindingType::ConstantBufferView: return "CBV";
				case RootBindingType::ShaderResourceView: return "SRV";
				case RootBindingType::UnorderedAccessView: return "UAV";
				case RootBindingType::Constants: return "Root Constants";
				default: return "";
			}
		}

		RenderPass::ResourceBindingDiagnostic BuildRootBindingDiagnostic(
			const RootBindingState& rootBinding,
			UINT rootParameterIndex,
			bool computePipeline)
		{
			RenderPass::ResourceBindingDiagnostic binding{};
			binding.pipeline = computePipeline ? "Compute" : "Graphics";
			binding.bindingType = RootBindingTypeName(rootBinding.type);
			binding.rootParameterIndex = rootParameterIndex;
			if (rootBinding.type == RootBindingType::DescriptorTable)
				binding.gpuDescriptorHandle = rootBinding.value;
			else if (rootBinding.type == RootBindingType::Constants)
				binding.rootConstants = rootBinding.constants;
			else
				binding.gpuAddress = rootBinding.value;
			return binding;
		}

		RenderPassMipChain::RootArgumentSnapshot BuildRootArgumentSnapshot(
			const RootBindingState& rootBinding,
			UINT rootParameterIndex)
		{
			RenderPassMipChain::RootArgumentSnapshot binding{};
			binding.rootParameterIndex = rootParameterIndex;
			binding.value = rootBinding.value;
			switch (rootBinding.type)
			{
				case RootBindingType::DescriptorTable:
					binding.type = RenderPassMipChain::RootArgumentType::DescriptorTable;
					break;
				case RootBindingType::ConstantBufferView:
					binding.type = RenderPassMipChain::RootArgumentType::ConstantBufferView;
					break;
				case RootBindingType::ShaderResourceView:
					binding.type = RenderPassMipChain::RootArgumentType::ShaderResourceView;
					break;
				case RootBindingType::UnorderedAccessView:
					binding.type = RenderPassMipChain::RootArgumentType::UnorderedAccessView;
					break;
				case RootBindingType::Constants:
					binding.type = RenderPassMipChain::RootArgumentType::Constants;
					binding.constants = rootBinding.constants;
					break;
				default:
					break;
			}
			return binding;
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

		bool ResolvedModifiedShaderMatches(
			const RenderPassConfigurationSnapshot& configuration,
			const RenderPass::RenderPassDisk& renderPass,
			const ShaderTargetBinding& shaderTarget)
		{
			const ResolvedEventBinding* resolvedEvent =
				FindResolvedEventBinding(configuration, renderPass);
			return resolvedEvent && resolvedEvent->modifiedShaderId == shaderTarget.modifiedShaderId;
		}

		bool HasLinkedShaderTargetBinding(
			const RenderPassConfigurationSnapshot& configuration,
			const RenderPass::RenderPassDisk& renderPass,
			const ShaderTargetBindingMap& shaderTargetBindings)
		{
			for (const auto& shaderTargetBinding : shaderTargetBindings)
			{
				if (ResolvedModifiedShaderMatches(configuration, renderPass, shaderTargetBinding.second))
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
				const ResolvedEventBinding* resolvedEvent =
					FindResolvedEventBinding(configuration, renderPass);
				if (!renderPass.enabled || !renderPass.trackResourceBindings ||
					!resolvedEvent ||
					!HasLinkedShaderTargetBinding(configuration, renderPass, shaderTargetBindings))
				{
					continue;
				}

				const auto diagnosticsIt = gDiagnosticsByRenderPassId.find(renderPass.id);
				const bool currentSnapshotAvailable = diagnosticsIt != gDiagnosticsByRenderPassId.end() &&
					diagnosticsIt->second.resourceSnapshotCaptured &&
					diagnosticsIt->second.lastModifiedShaderId == resolvedEvent->modifiedShaderId;
				if (!currentSnapshotAvailable)
					gPendingResourceSnapshotIds.insert(renderPass.id);
			}

			gResourceTrackingRequired.store(!gPendingResourceSnapshotIds.empty(), std::memory_order_release);
			RefreshTrackingModeFlags();
		}

		void RefreshExecutionTrackingFlags(
			const RenderPassConfigurationSnapshot& configuration,
			const ShaderTargetBindingMap& shaderTargetBindings)
		{
			uint32_t graphicsBoundaryMask = 0;
			uint32_t computeBoundaryMask = 0;
			for (const auto& shaderTargetBinding : shaderTargetBindings)
			{
				const auto executionPlanIt =
					configuration.executionPlans.find(shaderTargetBinding.second.modifiedShaderId);
				if (executionPlanIt == configuration.executionPlans.end())
					continue;

				if (shaderTargetBinding.second.type == ShaderTarget::ComputeShader)
					computeBoundaryMask |= executionPlanIt->second.computeBoundaryMask;
				else
					graphicsBoundaryMask |= executionPlanIt->second.graphicsBoundaryMask;
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
				gExecutionTrackingGeneration.load(std::memory_order_relaxed);
			if (!state.pipelineState)
				return;

			const ShaderTargetBindingMap* shaderTargetBindings =
				gPublishedShaderTargetBindings.load(std::memory_order_acquire);
			const auto shaderTargetIt = shaderTargetBindings->find(state.pipelineState);
			if (shaderTargetIt == shaderTargetBindings->end())
				return;

			const RenderPassConfigurationSnapshot* configuration =
				gPublishedConfiguration.load(std::memory_order_acquire);
			const auto executionPlanIt =
				configuration->executionPlans.find(shaderTargetIt->second.modifiedShaderId);
			if (executionPlanIt == configuration->executionPlans.end())
				return;

			if (shaderTargetIt->second.type == ShaderTarget::ComputeShader)
				state.computeExecutionBoundaryMask = executionPlanIt->second.computeBoundaryMask;
			else
				state.graphicsExecutionBoundaryMask = executionPlanIt->second.graphicsBoundaryMask;
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

		const std::vector<RenderPassResourceRegistry::DescriptorBindingLocation>*
			FindGameTextureBindingLocations(
				const RenderPass::RenderPassDisk& renderPass,
				const RenderPass::LogicalResourceBindingDisk& binding,
				ID3D12RootSignature* rootSignature,
				bool computePipeline)
		{
			for (const ThreadGameTextureBindingLookup& lookup : gGameTextureBindingLookups)
			{
				if (lookup.renderPass == &renderPass && lookup.rootSignature == rootSignature &&
					lookup.viewType == binding.gameResourceViewType &&
					lookup.shaderRegister == binding.shaderRegister &&
					lookup.registerSpace == binding.registerSpace)
				{
					return lookup.locations.empty() ? nullptr : &lookup.locations;
				}
			}

			ThreadGameTextureBindingLookup lookup{};
			lookup.renderPass = &renderPass;
			lookup.rootSignature = rootSignature;
			lookup.viewType = binding.gameResourceViewType;
			lookup.shaderRegister = binding.shaderRegister;
			lookup.registerSpace = binding.registerSpace;
			const D3D12_DESCRIPTOR_RANGE_TYPE rangeType =
				binding.gameResourceViewType == RenderPass::GameResourceViewType::UnorderedAccess
					? D3D12_DESCRIPTOR_RANGE_TYPE_UAV
					: D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			RenderPassResourceRegistry::GetDescriptorBindingCandidates(
				rootSignature,
				rangeType,
				binding.shaderRegister,
				binding.registerSpace,
				renderPass.maximumTrackedDescriptors,
				computePipeline ? D3D12_SHADER_VISIBILITY_ALL : D3D12_SHADER_VISIBILITY_PIXEL,
				lookup.locations);

			if (lookup.locations.empty())
				return nullptr;
			ThreadGameTextureBindingLookup& destination = gGameTextureBindingLookups[gNextGameTextureBindingLookup];
			destination = std::move(lookup);
			gNextGameTextureBindingLookup =
				(gNextGameTextureBindingLookup + 1) % gGameTextureBindingLookups.size();
			return &destination.locations;
		}

		bool ResolveGameTexture(
			const RenderPass::RenderPassDisk& renderPass,
			const RenderPass::LogicalResourceBindingDisk& binding,
			const CommandListRenderState& state,
			bool computePipeline,
			RenderPassTexturePool::TextureView& outTexture,
			std::string& outError)
		{
			outTexture = {};
			ID3D12RootSignature* rootSignature = computePipeline
				? state.computeRootSignature
				: state.graphicsRootSignature;
			if (!rootSignature)
			{
				outError = "The game texture's root signature is not currently bound.";
				return false;
			}

			const auto* locations = FindGameTextureBindingLocations(
				renderPass,
				binding,
				rootSignature,
				computePipeline);
			const char registerPrefix = binding.gameResourceViewType ==
				RenderPass::GameResourceViewType::UnorderedAccess ? 'u' : 't';
			if (!locations)
			{
				outError = StringHelper::Format(
					"The target root signature does not expose %c%u, space%u.",
					registerPrefix,
					binding.shaderRegister,
					binding.registerSpace);
				return false;
			}

			const std::vector<RootBindingState>& rootBindings = RootBindings(state, computePipeline);
			const char* expectedBindingType = binding.gameResourceViewType ==
				RenderPass::GameResourceViewType::UnorderedAccess ? "UAV" : "SRV";
			for (const RenderPassResourceRegistry::DescriptorBindingLocation& location : *locations)
			{
				if (location.rootParameterIndex >= rootBindings.size())
					continue;
				const RootBindingState& rootBinding = rootBindings[location.rootParameterIndex];
				if (rootBinding.type != RootBindingType::DescriptorTable || !rootBinding.value)
					continue;

				for (const DescriptorHeapState& heap : state.descriptorHeaps)
				{
					if (heap.type != location.heapType || !heap.gpuStart.ptr ||
						!heap.cpuStart.ptr || !heap.descriptorIncrementSize || !heap.descriptorCount)
					{
						continue;
					}
					const uint64_t heapByteSize = static_cast<uint64_t>(heap.descriptorIncrementSize) *
						heap.descriptorCount;
					if (rootBinding.value < heap.gpuStart.ptr ||
						rootBinding.value >= heap.gpuStart.ptr + heapByteSize)
					{
						continue;
					}

					const uint64_t descriptorByteOffset = rootBinding.value - heap.gpuStart.ptr +
						static_cast<uint64_t>(location.tableOffset) * heap.descriptorIncrementSize;
					if (descriptorByteOffset >= heapByteSize)
						continue;
					RenderPass::ResourceBindingDiagnostic metadata{};
					if (!RenderPassResourceRegistry::ResolveDescriptor(
						{ heap.cpuStart.ptr + static_cast<SIZE_T>(descriptorByteOffset) },
						metadata) || metadata.bindingType != expectedBindingType ||
						!metadata.resourcePointer)
					{
						continue;
					}

					ID3D12Resource* resource = reinterpret_cast<ID3D12Resource*>(metadata.resourcePointer);
					const D3D12_RESOURCE_DESC description = resource->GetDesc();
					if (description.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
						description.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D)
					{
						outError = "The selected game binding is not a Texture2D or Texture3D resource.";
						return false;
					}

					outTexture.resource = resource;
					outTexture.description.dimension = description.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
						? ShaderResource::TextureDimension::Texture3D
						: (description.DepthOrArraySize > 1
							? ShaderResource::TextureDimension::Texture2DArray
							: ShaderResource::TextureDimension::Texture2D);
					outTexture.description.format = description.Format;
					outTexture.description.width = static_cast<uint32_t>((std::min)(
						description.Width,
						static_cast<UINT64>((std::numeric_limits<uint32_t>::max)())));
					outTexture.description.height = description.Height;
					outTexture.description.depth = description.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
						? description.DepthOrArraySize
						: 1u;
					outTexture.description.arraySize = description.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
						? 1u
						: description.DepthOrArraySize;
					outTexture.description.mipLevels = description.MipLevels;
					outTexture.description.sampleCount = description.SampleDesc.Count;
					outTexture.description.flags = description.Flags;
					outTexture.initialState = binding.gameResourceViewType ==
						RenderPass::GameResourceViewType::UnorderedAccess
							? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
							: (computePipeline
								? D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
								: D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
					return true;
				}
			}

			outError = StringHelper::Format(
				"No live %s texture metadata is available for %c%u, space%u. Reload the scene or application after saving this pass if the descriptor predates tracking.",
				expectedBindingType,
				registerPrefix,
				binding.shaderRegister,
				binding.registerSpace);
			return false;
		}

		RenderPassTexturePool::ReferenceExtent BuildTextureReference(
			const RenderPassTexturePool::TextureView& texture)
		{
			RenderPassTexturePool::ReferenceExtent reference{};
			reference.dimension = texture.description.dimension;
			reference.width = texture.description.width;
			reference.height = texture.description.height;
			reference.depth = texture.description.depth;
			reference.arraySize = texture.description.arraySize;
			reference.mipLevels = texture.description.mipLevels;
			reference.sampleCount = texture.description.sampleCount;
			reference.fallbackFormat = texture.description.format;
			return reference;
		}

		std::vector<RenderPass::ResourceBindingDiagnostic> BuildResourceSnapshot(
			const CommandListRenderState& state,
			bool computePipeline,
			uint32_t maximumTrackedDescriptors)
		{
			std::vector<RenderPass::ResourceBindingDiagnostic> bindings;
			const std::vector<RootBindingState>& rootBindings = RootBindings(state, computePipeline);
			bindings.reserve(state.descriptorHeaps.size() + rootBindings.size());

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
			for (UINT rootParameterIndex = 0;
				rootParameterIndex < rootBindings.size();
				++rootParameterIndex)
			{
				const RootBindingState& rootBinding = rootBindings[rootParameterIndex];
				if (rootBinding.type == RootBindingType::None)
					continue;

				RenderPass::ResourceBindingDiagnostic binding = BuildRootBindingDiagnostic(
					rootBinding,
					rootParameterIndex,
					computePipeline);
				if (rootBinding.type == RootBindingType::DescriptorTable)
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

		void BuildMipChainGraphicsState(
			const CommandListRenderState& state,
			RenderPassMipChain::GraphicsStateSnapshot& snapshot)
		{
			snapshot.rootSignature = state.graphicsRootSignature;
			snapshot.pipelineState = state.boundPipelineState;
			snapshot.primitiveTopology = state.primitiveTopology;
			snapshot.viewports.assign(state.viewports.begin(), state.viewports.end());
			snapshot.scissorRectangles.assign(
				state.scissorRectangles.begin(),
				state.scissorRectangles.end());

			snapshot.descriptorHeaps.clear();
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

			snapshot.rootBindings.clear();
			for (UINT rootParameterIndex = 0;
				rootParameterIndex < state.graphicsRootBindings.size();
				++rootParameterIndex)
			{
				const RootBindingState& rootBinding = state.graphicsRootBindings[rootParameterIndex];
				if (rootBinding.type != RootBindingType::None)
				{
					snapshot.rootBindings.push_back(BuildRootArgumentSnapshot(
						rootBinding,
						rootParameterIndex));
				}
			}

			UINT renderTargetCount = 0;
			for (const RenderPass::ResourceBindingDiagnostic& binding : state.outputBindings)
			{
				if (binding.bindingType == "RTV" && binding.descriptorIndex != UINT32_MAX)
					renderTargetCount = (std::max)(renderTargetCount, binding.descriptorIndex + 1);
			}
			snapshot.renderTargets.assign(renderTargetCount, {});
			snapshot.depthStencil = {};
			for (const RenderPass::ResourceBindingDiagnostic& binding : state.outputBindings)
			{
				if (binding.bindingType == "RTV" && binding.descriptorIndex < snapshot.renderTargets.size())
					snapshot.renderTargets[binding.descriptorIndex].ptr = binding.cpuDescriptorHandle;
				else if (binding.bindingType == "DSV")
					snapshot.depthStencil.ptr = binding.cpuDescriptorHandle;
			}
		}

		void BuildShaderResourceState(
			const CommandListRenderState& state,
			bool computePipeline,
			RenderPassMipChain::GraphicsStateSnapshot& snapshot)
		{
			snapshot.pipelineState = state.boundPipelineState;
			snapshot.primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
			snapshot.viewports.clear();
			snapshot.scissorRectangles.clear();
			snapshot.renderTargets.clear();
			snapshot.depthStencil = {};
			snapshot.rootSignature = computePipeline ? state.computeRootSignature : state.graphicsRootSignature;
			snapshot.descriptorHeaps.clear();
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
			snapshot.rootBindings.clear();
			const std::vector<RootBindingState>& rootBindings = RootBindings(state, computePipeline);
			for (UINT rootParameterIndex = 0; rootParameterIndex < rootBindings.size(); ++rootParameterIndex)
			{
				if (rootBindings[rootParameterIndex].type != RootBindingType::None)
					snapshot.rootBindings.push_back(BuildRootArgumentSnapshot(
						rootBindings[rootParameterIndex], rootParameterIndex));
			}
		}

		RenderPassTexturePool::ReferenceExtent BuildRuntimeTextureReferenceExtent(
			const CommandListRenderState& state,
			const PipelineOutputState& pipelineOutputState)
		{
			RenderPassTexturePool::ReferenceExtent referenceExtent{};
			if (!state.viewports.empty())
			{
				referenceExtent.width = static_cast<uint32_t>(
					(std::max)(1.0, std::ceil(static_cast<double>(state.viewports.front().Width))));
				referenceExtent.height = static_cast<uint32_t>(
					(std::max)(1.0, std::ceil(static_cast<double>(state.viewports.front().Height))));
			}

			for (const RenderPass::ResourceBindingDiagnostic& output : state.outputBindings)
			{
				if (output.bindingType != "RTV")
					continue;
				if (!referenceExtent.width && output.resourceWidth)
					referenceExtent.width = static_cast<uint32_t>((std::min)(
						output.resourceWidth,
						static_cast<uint64_t>(UINT32_MAX)));
				if (!referenceExtent.height && output.resourceHeight)
					referenceExtent.height = output.resourceHeight;
				if (referenceExtent.fallbackFormat == DXGI_FORMAT_UNKNOWN && output.resourceFormat)
					referenceExtent.fallbackFormat = static_cast<DXGI_FORMAT>(output.resourceFormat);
			}

			if (referenceExtent.fallbackFormat == DXGI_FORMAT_UNKNOWN)
			{
				for (UINT renderTargetIndex = 0;
					renderTargetIndex < pipelineOutputState.renderTargetCount;
					++renderTargetIndex)
				{
					if (pipelineOutputState.renderTargetFormats[renderTargetIndex] != DXGI_FORMAT_UNKNOWN)
					{
						referenceExtent.fallbackFormat =
							pipelineOutputState.renderTargetFormats[renderTargetIndex];
						break;
					}
				}
			}
			return referenceExtent;
		}

		const RenderPass::LogicalResourceBindingDisk* FindRuntimeInput(
			const RenderPass::RenderPassDisk& renderPass,
			RenderPass::ResourceAccess preferredAccess)
		{
			const auto preferred = std::find_if(renderPass.inputs.begin(), renderPass.inputs.end(), [&](const auto& input)
			{
				return input.origin == ShaderResource::ResourceOrigin::Runtime &&
					input.access == preferredAccess && !input.resourceId.empty();
			});
			if (preferred != renderPass.inputs.end())
				return &*preferred;
			const auto shaderInput = std::find_if(renderPass.inputs.begin(), renderPass.inputs.end(), [](const auto& input)
			{
				return input.origin == ShaderResource::ResourceOrigin::Runtime &&
					input.access == RenderPass::ResourceAccess::ShaderResource && !input.resourceId.empty();
			});
			return shaderInput != renderPass.inputs.end() ? &*shaderInput : nullptr;
		}

		const RenderPass::LogicalResourceBindingDisk* FindCopyInput(
			const RenderPass::RenderPassDisk& renderPass)
		{
			const auto copyInput = std::find_if(renderPass.inputs.begin(), renderPass.inputs.end(), [](const auto& input)
			{
				return input.access == RenderPass::ResourceAccess::CopySource &&
					(input.origin == ShaderResource::ResourceOrigin::Game || !input.resourceId.empty());
			});
			if (copyInput != renderPass.inputs.end())
				return &*copyInput;
			return FindRuntimeInput(renderPass, RenderPass::ResourceAccess::ShaderResource);
		}

		const RenderPass::LogicalResourceBindingDisk* FindRuntimeOutput(
			const RenderPass::RenderPassDisk& renderPass,
			RenderPass::ResourceAccess preferredAccess)
		{
			const auto preferred = std::find_if(renderPass.outputs.begin(), renderPass.outputs.end(), [&](const auto& output)
			{
				return output.origin == ShaderResource::ResourceOrigin::Runtime &&
					output.access == preferredAccess && !output.resourceId.empty();
			});
			if (preferred != renderPass.outputs.end())
				return &*preferred;
			const auto renderTarget = std::find_if(renderPass.outputs.begin(), renderPass.outputs.end(), [](const auto& output)
			{
				return output.origin == ShaderResource::ResourceOrigin::Runtime &&
					output.access == RenderPass::ResourceAccess::RenderTarget && !output.resourceId.empty();
			});
			return renderTarget != renderPass.outputs.end() ? &*renderTarget : nullptr;
		}

		bool ResolveRuntimeTexture(
			const RenderPass::LogicalResourceBindingDisk* binding,
			RenderPassTexturePool::TextureView& outTexture,
			std::string& outError,
			const char* role,
			bool inputTexture = false)
		{
			outTexture = {};
			if (!binding)
			{
				outError = std::string("Render Pass has no runtime ") + role + " binding.";
				return false;
			}
			const bool found = inputTexture
				? RenderPassTexturePool::GetInputTexture(binding->resourceId, binding->temporalView, outTexture)
				: RenderPassTexturePool::GetTexture(binding->resourceId, binding->temporalView, outTexture);
			if (!found)
			{
				outError = std::string("Runtime ") + role + " texture is unavailable: " + binding->resourceId;
				return false;
			}
			return true;
		}

		bool ResolveComputeDispatch(
			const RenderPass::RenderPassDisk& renderPass,
			UINT originalThreadGroupCountX,
			UINT originalThreadGroupCountY,
			UINT originalThreadGroupCountZ,
			const std::vector<RenderPassTexturePool::TextureView>& unorderedAccessOutputs,
			UINT& outThreadGroupCountX,
			UINT& outThreadGroupCountY,
			UINT& outThreadGroupCountZ,
			std::string& outError)
		{
			outThreadGroupCountX = 0;
			outThreadGroupCountY = 0;
			outThreadGroupCountZ = 0;
			switch (renderPass.dispatch.mode)
			{
				case RenderPass::DispatchMode::InheritOriginal:
					outThreadGroupCountX = originalThreadGroupCountX;
					outThreadGroupCountY = originalThreadGroupCountY;
					outThreadGroupCountZ = originalThreadGroupCountZ;
					break;
				case RenderPass::DispatchMode::ExplicitThreadGroups:
					outThreadGroupCountX = renderPass.dispatch.explicitGroupCountX;
					outThreadGroupCountY = renderPass.dispatch.explicitGroupCountY;
					outThreadGroupCountZ = renderPass.dispatch.explicitGroupCountZ;
					break;
				case RenderPass::DispatchMode::ScaleByResolution:
				{
					uint32_t width = 0;
					uint32_t height = 0;
					uint32_t depth = 1;
					if (!unorderedAccessOutputs.empty())
					{
						width = unorderedAccessOutputs.front().description.width;
						height = unorderedAccessOutputs.front().description.height;
						depth = unorderedAccessOutputs.front().description.dimension == ShaderResource::TextureDimension::Texture3D
							? unorderedAccessOutputs.front().description.depth
							: 1u;
					}
					else if (renderPass.resolution.mode == ShaderResource::ResolutionMode::Explicit)
					{
						width = renderPass.resolution.width;
						height = renderPass.resolution.height;
					}
					if (!width || !height)
					{
						outError = "Scale-by-resolution dispatch requires a runtime UAV output or explicit pass resolution.";
						return false;
					}
					const uint32_t groupSizeX = (std::max)(1u, renderPass.dispatch.threadGroupSizeX);
					const uint32_t groupSizeY = (std::max)(1u, renderPass.dispatch.threadGroupSizeY);
					const uint32_t groupSizeZ = (std::max)(1u, renderPass.dispatch.threadGroupSizeZ);
					outThreadGroupCountX = (width + groupSizeX - 1u) / groupSizeX;
					outThreadGroupCountY = (height + groupSizeY - 1u) / groupSizeY;
					outThreadGroupCountZ = (depth + groupSizeZ - 1u) / groupSizeZ;
					break;
				}
			}

			if (!outThreadGroupCountX || !outThreadGroupCountY || !outThreadGroupCountZ)
			{
				outError = "Compute dispatch dimensions are unavailable for the selected dispatch policy.";
				return false;
			}
			return true;
		}

		void BuildModifiedShaderExecutionPlans(RenderPassConfigurationSnapshot& configuration)
		{
			configuration.executionPlans.clear();
			for (const auto& compiledPlanEntry : configuration.compiledGraph.executionPlans)
			{
				ModifiedShaderExecutionPlan& plan = configuration.executionPlans[compiledPlanEntry.first];
				plan.graphicsBoundaryMask = compiledPlanEntry.second.graphicsBoundaryMask;
				plan.computeBoundaryMask = compiledPlanEntry.second.computeBoundaryMask;
				for (size_t boundaryIndex = 0; boundaryIndex < 2; ++boundaryIndex)
				{
					for (size_t renderPassIndex : compiledPlanEntry.second.executionOrders[boundaryIndex])
					{
						if (renderPassIndex < configuration.renderPasses.size())
						{
							plan.executionOrders[boundaryIndex].push_back(&configuration.renderPasses[renderPassIndex]);
							plan.hasRuntimeResources = plan.hasRuntimeResources ||
								!configuration.renderPasses[renderPassIndex].runtimeResources.empty();
						}
					}
					for (size_t renderPassIndex : compiledPlanEntry.second.mipChainOrders[boundaryIndex])
					{
						if (renderPassIndex < configuration.renderPasses.size())
							plan.mipChainOrders[boundaryIndex].push_back(&configuration.renderPasses[renderPassIndex]);
					}
				}
			}
		}

		const ModifiedShaderExecutionPlan* FindModifiedShaderExecutionPlan(
			const RenderPassConfigurationSnapshot& configuration,
			const ShaderTargetBinding& shaderTarget)
		{
			const auto planIt = configuration.executionPlans.find(shaderTarget.modifiedShaderId);
			return planIt == configuration.executionPlans.end() ? nullptr : &planIt->second;
		}
	}

	void PublishRenderPassConfigurations(const std::vector<RenderPass::RenderPassDisk>& renderPasses)
	{
		RenderPassTexturePool::PublishConfigurations(renderPasses);
		auto snapshot = std::make_unique<RenderPassConfigurationSnapshot>();
		snapshot->renderPasses = renderPasses;
		snapshot->runtimeCounters.reserve(renderPasses.size());
		for (size_t renderPassIndex = 0; renderPassIndex < renderPasses.size(); ++renderPassIndex)
			snapshot->runtimeCounters.push_back(std::make_unique<RuntimeCounters>());
		BuildResolvedEventBindings(*snapshot);
		BuildModifiedShaderExecutionPlans(*snapshot);
		for (const std::string& diagnostic : snapshot->compiledGraph.diagnostics)
		{
			ShaderInjectorIO::WriteToLogFileWarning(
				"RenderPassRuntime->PublishRenderPassConfigurations: graph: " + diagnostic);
		}
		bool hasEnabledRenderPass = false;
		bool hasEnabledMipChainPass = false;
		bool hasShaderResourcePass = false;
		bool hasInheritedGameBinding = false;
		bool hasCustomFullscreenPass = false;
		bool hasGameTextureInput = false;
		std::unordered_set<std::string> activeIds;

		for (size_t renderPassIndex = 0; renderPassIndex < renderPasses.size(); ++renderPassIndex)
		{
			const RenderPass::RenderPassDisk& renderPass = renderPasses[renderPassIndex];
			activeIds.insert(renderPass.id);
			const bool graphNodeValid = renderPassIndex < snapshot->compiledGraph.nodes.size() &&
				snapshot->compiledGraph.nodes[renderPassIndex].valid;
			if (graphNodeValid)
			{
				const bool hasGameInput = std::any_of(
					renderPass.inputs.begin(),
					renderPass.inputs.end(),
					[](const RenderPass::LogicalResourceBindingDisk& input)
					{
						return input.origin == ShaderResource::ResourceOrigin::Game;
					});
				const bool hasRuntimeShaderInput = std::any_of(
					renderPass.inputs.begin(),
					renderPass.inputs.end(),
					[](const RenderPass::LogicalResourceBindingDisk& input)
					{
						return input.origin == ShaderResource::ResourceOrigin::Runtime &&
							input.access == RenderPass::ResourceAccess::ShaderResource;
					});
				const bool hasRuntimeUnorderedAccessOutput = std::any_of(
					renderPass.outputs.begin(),
					renderPass.outputs.end(),
					[](const RenderPass::LogicalResourceBindingDisk& output)
					{
						return output.origin == ShaderResource::ResourceOrigin::Runtime &&
							output.access == RenderPass::ResourceAccess::UnorderedAccess;
					});
				hasEnabledRenderPass = true;
				hasEnabledMipChainPass = hasEnabledMipChainPass ||
					renderPass.type == RenderPass::RenderPassType::MipChain;
				hasShaderResourcePass = hasShaderResourcePass ||
					!renderPass.shaderResources.empty() || hasRuntimeShaderInput ||
					hasRuntimeUnorderedAccessOutput || hasGameInput;
				hasInheritedGameBinding = hasInheritedGameBinding ||
					renderPass.inheritedGameBindings.shaderResources ||
					renderPass.inheritedGameBindings.constantBuffers;
				hasGameTextureInput = hasGameTextureInput || hasGameInput;
				hasCustomFullscreenPass = hasCustomFullscreenPass ||
					(renderPass.type == RenderPass::RenderPassType::Custom &&
						RenderPass::ResolveExecutionMode(renderPass) == RenderPass::ExecutionMode::FullscreenPixel);
			}
		}

		for (const ShaderResource::CatalogEntry& resource : ShaderResourceCatalog::GetSnapshot())
		{
			if (resource.origin == ShaderResource::ResourceOrigin::Runtime &&
				!resource.ownerRenderPassId.empty() &&
				activeIds.find(resource.ownerRenderPassId) == activeIds.end())
			{
				ShaderResourceCatalog::RemoveRuntimeResourcesByOwner(resource.ownerRenderPassId);
			}
		}
		for (const RenderPass::RenderPassDisk& renderPass : renderPasses)
		{
			if (RenderPass::FindMipChainRuntimeSource(renderPass))
			{
				ShaderResource::CatalogEntry entry{};
				entry.id = RenderPass::MipChainOutputResourceId(renderPass);
				entry.name = renderPass.name + " Mip Chain";
				entry.origin = ShaderResource::ResourceOrigin::Runtime;
				entry.lifetime = ShaderResource::ResourceLifetime::Persistent;
				entry.ownerRenderPassId = renderPass.id;
				entry.dimension = ShaderResource::TextureDimension::Texture2D;
				entry.status = "Declared mip chain";
				ShaderResourceCatalog::Upsert(entry);
			}
			for (const RenderPass::RuntimeResourceDefinitionDisk& definition : renderPass.runtimeResources)
			{
				if (definition.id.empty())
					continue;
				ShaderResource::CatalogEntry entry{};
				entry.id = definition.id;
				entry.name = definition.name.empty() ? definition.id : definition.name;
				entry.origin = ShaderResource::ResourceOrigin::Runtime;
				entry.lifetime = definition.texture.lifetime;
				entry.ownerRenderPassId = renderPass.id;
				entry.dimension = definition.texture.dimension;
				entry.width = definition.texture.resolution.width;
				entry.height = definition.texture.resolution.height;
				entry.depth = definition.texture.depth;
				entry.arraySize = definition.texture.arraySize;
				entry.mipLevels = definition.texture.mipLevels;
				entry.format = definition.texture.format;
				entry.status = "Declared";
				ShaderResourceCatalog::Upsert(entry);
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
		gHasShaderResourcePasses.store(hasShaderResourcePass, std::memory_order_release);
		gHasInheritedGameBindings.store(hasInheritedGameBinding, std::memory_order_release);
		gHasCustomFullscreenPasses.store(hasCustomFullscreenPass, std::memory_order_release);
		gHasGameTextureInputs.store(hasGameTextureInput, std::memory_order_release);
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
			for (const RenderPass::RenderPassDisk& renderPass : renderPasses)
			{
				if (renderPass.trackResourceBindings)
					continue;
				const auto diagnosticsIt = gDiagnosticsByRenderPassId.find(renderPass.id);
				if (diagnosticsIt != gDiagnosticsByRenderPassId.end())
				{
					diagnosticsIt->second.resourceSnapshotCaptured = false;
					diagnosticsIt->second.resourceBindings.clear();
				}
			}
		}
		RefreshPendingResourceSnapshots(*snapshotPointer, *shaderTargetBindings);
	}

	bool HasEnabledRenderPasses()
	{
		return gHasEnabledRenderPasses.load(std::memory_order_relaxed);
	}

	bool HasEnabledMipChainPasses()
	{
		return gHasEnabledMipChainPasses.load(std::memory_order_relaxed);
	}

	bool IsTrackingRequired()
	{
		return (gTrackingModeFlags.load(std::memory_order_relaxed) & TrackingEnabled) != 0;
	}

	bool IsResourceTrackingRequired()
	{
		return (gTrackingModeFlags.load(std::memory_order_relaxed) & ResourceTrackingEnabled) != 0;
	}

	bool IsRootBindingTrackingRequired()
	{
		return (gTrackingModeFlags.load(std::memory_order_relaxed) &
			RootBindingTrackingEnabled) != 0;
	}

	bool IsGameTextureDescriptorTrackingRequired()
	{
		return gHasGameTextureInputs.load(std::memory_order_relaxed);
	}

	bool IsDescriptorTableTrackingRequired()
	{
		return (gTrackingModeFlags.load(std::memory_order_relaxed) &
			DescriptorTableTrackingEnabled) != 0;
	}

	bool IsDescriptorRegistryTrackingRequired()
	{
		return (gTrackingModeFlags.load(std::memory_order_relaxed) &
			DescriptorRegistryTrackingEnabled) != 0;
	}

	bool IsGraphicsStateTrackingRequired()
	{
		return (gTrackingModeFlags.load(std::memory_order_relaxed) &
			GraphicsStateTrackingEnabled) != 0;
	}

	bool IsPipelineExecutionTrackingRequired(bool computePipeline)
	{
		return (computePipeline
			? gComputeExecutionBoundaryMask.load(std::memory_order_relaxed)
			: gGraphicsExecutionBoundaryMask.load(std::memory_order_relaxed)) != 0;
	}

	bool HasPendingCommandListSubmissionWork()
	{
		return RenderPassMipChain::HasRecordedCommandListWork() ||
			ShaderResourceRuntime::HasRecordedCommandListWork();
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
		const uint32_t requiredBoundary = boundary == ExecutionBoundary::After ? 2u : 1u;
		return (GetExecutionBoundaryMask(commandList, computePipeline) & requiredBoundary) != 0;
	}

	uint32_t GetExecutionBoundaryMask(
		ID3D12GraphicsCommandList* commandList,
		bool computePipeline)
	{
		if (!commandList)
			return 0;
		const uint32_t globalBoundaryMask = computePipeline
			? gComputeExecutionBoundaryMask.load(std::memory_order_relaxed)
			: gGraphicsExecutionBoundaryMask.load(std::memory_order_relaxed);
		if (!globalBoundaryMask)
			return 0;

		CommandListRenderState& state = GetCommandListState(commandList);
		const uint64_t currentGeneration =
			gExecutionTrackingGeneration.load(std::memory_order_relaxed);
		if (state.executionTrackingGeneration != currentGeneration)
			RefreshCommandListExecutionBoundaries(state);

		return computePipeline
			? state.computeExecutionBoundaryMask
			: state.graphicsExecutionBoundaryMask;
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
		ResetRootBindings(state.graphicsRootBindings);
		ResetRootBindings(state.computeRootBindings);
		state.inputBindings.clear();
		state.outputBindings.clear();
		state.viewports.clear();
		state.scissorRectangles.clear();
		RefreshCommandListExecutionBoundaries(state);
	}

	void CompleteCommandListReset(ID3D12GraphicsCommandList* commandList, bool resetSucceeded)
	{
		if (resetSucceeded)
		{
			RenderPassMipChain::ResetCommandListRecording(commandList);
			ShaderResourceRuntime::ResetCommandList(commandList);
		}
	}

	void TrackPipelineState(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* pipelineState)
	{
		if (!IsTrackingRequired())
			return;
		PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Timing::TrackPipelineState, 64);

		CommandListRenderState& state = GetCommandListState(commandList);
		if (state.pipelineState == pipelineState)
			return;
		state.pipelineState = pipelineState;
		state.boundPipelineState = pipelineState;
		RefreshCommandListExecutionBoundaries(state);
	}

	void TrackBoundPipelineState(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* pipelineState)
	{
		if (!IsTrackingRequired())
			return;

		CommandListRenderState& state = GetCommandListState(commandList);
		if (state.boundPipelineState != pipelineState)
			state.boundPipelineState = pipelineState;
	}

	void TrackPrimitiveTopology(
		ID3D12GraphicsCommandList* commandList,
		D3D12_PRIMITIVE_TOPOLOGY primitiveTopology)
	{
		if (!IsTrackingRequired())
			return;
		PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Timing::TrackGraphicsState, 64);

		CommandListRenderState& state = GetCommandListState(commandList);
		if (state.primitiveTopology != primitiveTopology)
			state.primitiveTopology = primitiveTopology;
	}

	void TrackRootSignature(
		ID3D12GraphicsCommandList* commandList,
		bool computePipeline,
		ID3D12RootSignature* rootSignature)
	{
		if (!IsTrackingRequired())
			return;
		PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Timing::TrackGraphicsState, 64);

		CommandListRenderState& state = GetCommandListState(commandList);
		ID3D12RootSignature*& currentRootSignature = computePipeline
			? state.computeRootSignature
			: state.graphicsRootSignature;
		if (currentRootSignature == rootSignature)
			return;

		currentRootSignature = rootSignature;
		if (!computePipeline)
			HookD3D12::EnsureRenderPassRootSignatureRegistered(rootSignature);
		ResetRootBindings(RootBindings(state, computePipeline));
	}

	void TrackDescriptorHeaps(
		ID3D12GraphicsCommandList* commandList,
		UINT descriptorHeapCount,
		ID3D12DescriptorHeap* const* descriptorHeaps)
	{
		if (!IsTrackingRequired())
			return;
		PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Timing::TrackDescriptorHeaps, 64);

		CommandListRenderState& state = GetCommandListState(commandList);
		bool heapsUnchanged = state.descriptorHeaps.size() == descriptorHeapCount;
		for (UINT heapIndex = 0; heapsUnchanged && heapIndex < descriptorHeapCount; ++heapIndex)
		{
			heapsUnchanged = descriptorHeaps &&
				state.descriptorHeaps[heapIndex].heap == descriptorHeaps[heapIndex];
		}
		if (heapsUnchanged)
			return;

		state.descriptorHeaps.clear();
		state.descriptorHeaps.reserve(descriptorHeapCount);
		ResetRootBindings(state.graphicsRootBindings, true);
		ResetRootBindings(state.computeRootBindings, true);

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
			RenderPassResourceRegistry::RegisterDescriptorHeap(
				descriptorHeap,
				heap.descriptorIncrementSize);

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
		PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Timing::TrackRootDescriptorTable, 64);

		CommandListRenderState& state = GetCommandListState(commandList);
		RootBindingState& binding = RootBindingAt(state, computePipeline, rootParameterIndex);
		if (binding.type == RootBindingType::DescriptorTable && binding.value == descriptorHandle.ptr)
			return;
		binding.type = RootBindingType::DescriptorTable;
		binding.value = descriptorHandle.ptr;
		binding.constants.clear();
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

		RootBindingType rootBindingType = RootBindingType::ConstantBufferView;
		if (bindingType && bindingType[0] == 'S')
			rootBindingType = RootBindingType::ShaderResourceView;
		else if (bindingType && bindingType[0] == 'U')
			rootBindingType = RootBindingType::UnorderedAccessView;

		RootBindingState& binding = RootBindingAt(
			GetCommandListState(commandList),
			computePipeline,
			rootParameterIndex);
		if (binding.type == rootBindingType && binding.value == gpuAddress)
			return;
		binding.type = rootBindingType;
		binding.value = gpuAddress;
		binding.constants.clear();
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
		RootBindingState& binding = RootBindingAt(state, computePipeline, rootParameterIndex);
		if (binding.type != RootBindingType::Constants)
		{
			binding.type = RootBindingType::Constants;
			binding.value = 0;
			binding.constants.clear();
		}

		if (!values)
			return;

		if (destinationOffset > UINT_MAX - valueCount)
			return;
		const UINT capturedValueCount = valueCount;
		const size_t requiredSize = destinationOffset + capturedValueCount;
		const uint32_t* sourceValues = static_cast<const uint32_t*>(values);
		if (binding.constants.size() >= requiredSize &&
			std::equal(
				sourceValues,
				sourceValues + capturedValueCount,
				binding.constants.begin() + destinationOffset))
		{
			return;
		}
		if (binding.constants.size() < requiredSize)
			binding.constants.resize(requiredSize);

		std::copy(
			sourceValues,
			sourceValues + capturedValueCount,
			binding.constants.begin() + destinationOffset);
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
		PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Timing::TrackGraphicsState, 64);

		CommandListRenderState& state = GetCommandListState(commandList);
		EnsureDescriptorIncrementSizes(commandList, state);
		const UINT capturedRenderTargetCount = renderTargetDescriptors ? renderTargetCount : 0;
		const bool hasDepthStencil = depthStencilDescriptor && depthStencilDescriptor->ptr;
		const size_t expectedBindingCount =
			static_cast<size_t>(capturedRenderTargetCount) + (hasDepthStencil ? 1u : 0u);
		bool outputsUnchanged = state.outputBindings.size() == expectedBindingCount;
		const UINT renderTargetIncrement = state.descriptorIncrementSizes[D3D12_DESCRIPTOR_HEAP_TYPE_RTV];
		for (UINT renderTargetIndex = 0;
			outputsUnchanged && renderTargetIndex < capturedRenderTargetCount;
			++renderTargetIndex)
		{
			const D3D12_CPU_DESCRIPTOR_HANDLE descriptor = descriptorsAreContiguous
				? D3D12_CPU_DESCRIPTOR_HANDLE{
					renderTargetDescriptors[0].ptr +
					static_cast<SIZE_T>(renderTargetIndex) * renderTargetIncrement }
				: renderTargetDescriptors[renderTargetIndex];
			const RenderPass::ResourceBindingDiagnostic& binding = state.outputBindings[renderTargetIndex];
			outputsUnchanged = binding.bindingType == "RTV" &&
				binding.cpuDescriptorHandle == descriptor.ptr &&
				binding.descriptorIndex == renderTargetIndex;
		}
		if (outputsUnchanged && hasDepthStencil)
		{
			const RenderPass::ResourceBindingDiagnostic& binding = state.outputBindings.back();
			outputsUnchanged = binding.bindingType == "DSV" &&
				binding.cpuDescriptorHandle == depthStencilDescriptor->ptr;
		}
		if (outputsUnchanged)
			return;

		state.outputBindings.resize(capturedRenderTargetCount + (hasDepthStencil ? 1u : 0u));

		for (UINT renderTargetIndex = 0; renderTargetIndex < capturedRenderTargetCount; ++renderTargetIndex)
		{
			D3D12_CPU_DESCRIPTOR_HANDLE descriptor = descriptorsAreContiguous
				? D3D12_CPU_DESCRIPTOR_HANDLE{
					renderTargetDescriptors[0].ptr + static_cast<SIZE_T>(renderTargetIndex) * renderTargetIncrement }
				: renderTargetDescriptors[renderTargetIndex];
			RenderPass::ResourceBindingDiagnostic& binding = state.outputBindings[renderTargetIndex];
			if (binding.bindingType != "RTV")
			{
				binding = {};
				binding.pipeline = "Graphics";
				binding.bindingType = "RTV";
			}
			binding.cpuDescriptorHandle = descriptor.ptr;
			binding.descriptorIndex = renderTargetIndex;
		}

		if (hasDepthStencil)
		{
			RenderPass::ResourceBindingDiagnostic& binding =
				state.outputBindings[capturedRenderTargetCount];
			if (binding.bindingType != "DSV")
			{
				binding = {};
				binding.pipeline = "Graphics";
				binding.bindingType = "DSV";
			}
			binding.cpuDescriptorHandle = depthStencilDescriptor->ptr;
			binding.descriptorIndex = 0;
		}
	}

	void TrackViewports(
		ID3D12GraphicsCommandList* commandList,
		UINT viewportCount,
		const D3D12_VIEWPORT* viewports)
	{
		if (!IsTrackingRequired())
			return;
		PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Timing::TrackGraphicsState, 64);

		CommandListRenderState& state = GetCommandListState(commandList);
		if (viewports && viewportCount == state.viewports.size() &&
			(viewportCount == 0 || std::memcmp(
				viewports,
				state.viewports.data(),
				static_cast<size_t>(viewportCount) * sizeof(D3D12_VIEWPORT)) == 0))
		{
			return;
		}
		if (viewports && viewportCount)
			state.viewports.assign(viewports, viewports + viewportCount);
		else
			state.viewports.clear();
	}

	void TrackScissorRectangles(
		ID3D12GraphicsCommandList* commandList,
		UINT rectangleCount,
		const D3D12_RECT* rectangles)
	{
		if (!IsTrackingRequired())
			return;
		PerformanceMetrics::ScopedTimer timer(PerformanceMetrics::Timing::TrackGraphicsState, 64);

		CommandListRenderState& state = GetCommandListState(commandList);
		if (rectangles && rectangleCount == state.scissorRectangles.size() &&
			(rectangleCount == 0 || std::memcmp(
				rectangles,
				state.scissorRectangles.data(),
				static_cast<size_t>(rectangleCount) * sizeof(D3D12_RECT)) == 0))
		{
			return;
		}
		if (rectangles && rectangleCount)
			state.scissorRectangles.assign(rectangles, rectangles + rectangleCount);
		else
			state.scissorRectangles.clear();
	}

	void RecordExecutionBoundary(
		ID3D12GraphicsCommandList* commandList,
		bool computePipeline,
		ExecutionBoundary boundary,
		const char* operationName,
		UINT originalThreadGroupCountX,
		UINT originalThreadGroupCountY,
		UINT originalThreadGroupCountZ)
	{
		if (!IsTrackingRequired())
			return;
		PerformanceMetrics::Increment(PerformanceMetrics::Counter::ExecutionBoundaryRecorded);
		PerformanceMetrics::ScopedTimer boundaryTimer(
			PerformanceMetrics::Timing::RecordExecutionBoundary);

		CommandListRenderState& state = GetCommandListState(commandList);
		if (!state.pipelineState)
			return;

		const ShaderTargetBindingMap* shaderTargetBindings =
			gPublishedShaderTargetBindings.load(std::memory_order_acquire);
		const auto targetIt = shaderTargetBindings->find(state.pipelineState);
		if (targetIt == shaderTargetBindings->end())
			return;

		const RenderPassConfigurationSnapshot* configuration =
			gPublishedConfiguration.load(std::memory_order_acquire);
		const ModifiedShaderExecutionPlan* executionPlan =
			FindModifiedShaderExecutionPlan(*configuration, targetIt->second);
		if (!executionPlan)
			return;
		const size_t boundaryIndex = boundary == ExecutionBoundary::After ? 1u : 0u;
		const std::vector<const RenderPass::RenderPassDisk*>& executionOrder =
			executionPlan->executionOrders[boundaryIndex];
		if (executionOrder.empty())
			return;
		RenderPassTexturePool::ScopedInputTextureOverrides inputTextureOverrides;

		thread_local std::vector<RenderPassMipChain::ExecutionResult> mipChainResults;
		mipChainResults.clear();
		thread_local std::unordered_set<std::string> unavailableRuntimeResources;
		unavailableRuntimeResources.clear();
		bool mipChainsPrepared = false;
		RenderPassTexturePool::ReferenceExtent runtimeTextureReference{};
		bool runtimeTextureReferenceBuilt = false;
		bool shaderResourceStateBuilt = false;
		bool fullscreenGraphicsStateBuilt = false;
		const auto captureShaderResourceState = [&]
		{
			if (!shaderResourceStateBuilt)
			{
				BuildShaderResourceState(state, computePipeline, gShaderResourceState);
				BuildShaderResourceState(state, !computePipeline, gOppositeShaderResourceState);
				shaderResourceStateBuilt = true;
			}
		};
		const auto captureFullscreenGraphicsState = [&]
		{
			if (!fullscreenGraphicsStateBuilt)
			{
				BuildMipChainGraphicsState(state, gFullscreenGraphicsState);
				fullscreenGraphicsStateBuilt = true;
			}
		};
		for (const RenderPass::RenderPassDisk* renderPassPointer : executionOrder)
		{
			const RenderPass::RenderPassDisk& renderPass = *renderPassPointer;
			const RenderPass::PassOperation passOperation = RenderPass::ResolvePassOperation(renderPass);
			const ResolvedEventBinding* resolvedEvent =
				FindResolvedEventBinding(*configuration, renderPass);
			if (!resolvedEvent)
			{
				continue;
			}

			bool executionAttempted = false;
			bool executionSucceeded = false;
			std::string executionError;
			const RenderPass::LogicalResourceBindingDisk* mipRuntimeSource =
				RenderPass::FindMipChainRuntimeSource(renderPass);
			RenderPassTexturePool::TextureView generatedMipTexture;
			bool dependenciesReady = true;
			for (const RenderPass::LogicalResourceBindingDisk& input : renderPass.inputs)
			{
				if (input.origin != ShaderResource::ResourceOrigin::Runtime || input.optional ||
					input.resourceId.empty() ||
					unavailableRuntimeResources.find(input.resourceId) == unavailableRuntimeResources.end())
				{
					continue;
				}

				dependenciesReady = false;
				executionError = "A required runtime texture was not produced during this execution: " +
					input.resourceId;
				break;
			}
			if (!runtimeTextureReferenceBuilt && !renderPass.runtimeResources.empty() &&
				passOperation != RenderPass::PassOperation::Copy)
			{
				runtimeTextureReference = BuildRuntimeTextureReferenceExtent(state, targetIt->second.outputState);
				runtimeTextureReferenceBuilt = true;
			}
			RenderPassTexturePool::ReferenceExtent passTextureReference = runtimeTextureReference;
			if (computePipeline && originalThreadGroupCountX && originalThreadGroupCountY)
			{
				passTextureReference.width = originalThreadGroupCountX *
					(std::max)(1u, renderPass.dispatch.threadGroupSizeX);
				passTextureReference.height = originalThreadGroupCountY *
					(std::max)(1u, renderPass.dispatch.threadGroupSizeY);
			}

			const RenderPass::LogicalResourceBindingDisk* copySourceBinding = nullptr;
			RenderPassTexturePool::TextureView copySourceTexture;
			bool copySourceReady = dependenciesReady;
			if (dependenciesReady && passOperation == RenderPass::PassOperation::Copy)
			{
				copySourceBinding = FindCopyInput(renderPass);
				if (!copySourceBinding)
				{
					copySourceReady = false;
					executionError = "Render Pass has no copy source binding.";
				}
				else if (copySourceBinding->origin == ShaderResource::ResourceOrigin::Game)
				{
					copySourceReady = ResolveGameTexture(
						renderPass,
						*copySourceBinding,
						state,
						computePipeline,
						copySourceTexture,
						executionError);
				}
				else
				{
					copySourceReady = ResolveRuntimeTexture(
						copySourceBinding,
						copySourceTexture,
						executionError,
						"copy source",
						true);
				}
				if (copySourceReady)
					passTextureReference = BuildTextureReference(copySourceTexture);
			}

			const bool runtimeResourcesReady = dependenciesReady && copySourceReady &&
				(renderPass.runtimeResources.empty() ||
				RenderPassTexturePool::EnsurePassResources(
					renderPass,
					commandList,
					passTextureReference,
					executionError));
			if (!runtimeResourcesReady)
			{
				executionAttempted = true;
			}
			else if (mipRuntimeSource &&
				RenderPass::ResolveExecutionMode(renderPass) == (computePipeline
					? RenderPass::ExecutionMode::Compute : RenderPass::ExecutionMode::FullscreenPixel))
			{
				executionAttempted = true;
				RenderPassTexturePool::TextureView sourceTexture;
				if (ResolveRuntimeTexture(mipRuntimeSource, sourceTexture, executionError, "mip source", true))
				{
					captureShaderResourceState();
					if (!computePipeline)
						captureFullscreenGraphicsState();
					PerformanceMetrics::ScopedTimer prepareTimer(PerformanceMetrics::Timing::PrepareMipChains);
					executionSucceeded = RenderPassMipChain::GenerateRuntimeMipChain(
						renderPass,
						commandList,
						sourceTexture,
						computePipeline ? gShaderResourceState : gFullscreenGraphicsState,
						gOppositeShaderResourceState,
						computePipeline,
						generatedMipTexture,
						executionError);
				}
				PerformanceMetrics::Increment(PerformanceMetrics::Counter::MipPassAttempted);
				PerformanceMetrics::Increment(executionSucceeded
					? PerformanceMetrics::Counter::MipPassSucceeded : PerformanceMetrics::Counter::MipPassFailed);
			}
			else if (passOperation == RenderPass::PassOperation::MipChain && !mipRuntimeSource &&
				RenderPass::ResolveExecutionMode(renderPass) == (computePipeline
					? RenderPass::ExecutionMode::Compute
					: RenderPass::ExecutionMode::FullscreenPixel))
			{
				if (!mipChainsPrepared)
				{
					{
						PerformanceMetrics::ScopedTimer buildStateTimer(
							PerformanceMetrics::Timing::BuildMipGraphicsState);
						if (computePipeline)
							BuildShaderResourceState(state, true, gMipChainGraphicsState);
						else
							BuildMipChainGraphicsState(state, gMipChainGraphicsState);
					}
					{
						PerformanceMetrics::ScopedTimer prepareTimer(
							PerformanceMetrics::Timing::PrepareMipChains);
						RenderPassMipChain::PrepareForTargetDraw(
							executionPlan->mipChainOrders[boundaryIndex],
							commandList,
							gMipChainGraphicsState,
							computePipeline,
							mipChainResults);
					}
					gPendingMipRestore = std::any_of(
						mipChainResults.begin(),
						mipChainResults.end(),
						[](const auto& result) { return result.succeeded; });
					gPendingMipRestoreCommandList = gPendingMipRestore ? commandList : nullptr;
					mipChainsPrepared = true;
				}

				const auto resultIt = std::find_if(mipChainResults.begin(), mipChainResults.end(), [&](const auto& result)
				{
					return result.renderPass == &renderPass;
				});
				if (resultIt != mipChainResults.end())
				{
					executionAttempted = resultIt->attempted;
					executionSucceeded = resultIt->succeeded;
					executionError = resultIt->error;
					PerformanceMetrics::Increment(PerformanceMetrics::Counter::MipPassAttempted);
					PerformanceMetrics::Increment(executionSucceeded
						? PerformanceMetrics::Counter::MipPassSucceeded
						: PerformanceMetrics::Counter::MipPassFailed);
				}
			}
			else if (passOperation == RenderPass::PassOperation::Copy)
			{
				executionAttempted = true;
				RenderPassTexturePool::TextureView destinationTexture;
				if (ResolveRuntimeTexture(
						FindRuntimeOutput(renderPass, RenderPass::ResourceAccess::CopyDestination),
						destinationTexture,
						executionError,
						"copy destination"))
				{
					executionSucceeded = RenderPassExecutor::ExecuteTextureCopy(
						commandList,
						copySourceTexture,
						destinationTexture,
						executionError);
				}
			}
			else if (RenderPass::IsReplacementPass(renderPass.type) &&
				RenderPass::HasCompiledShaders(renderPass) &&
				((computePipeline && renderPass.type == RenderPass::RenderPassType::ReplacementComputeShader) ||
				(!computePipeline && renderPass.type == RenderPass::RenderPassType::ReplacementPixelShader)))
			{
				PerformanceMetrics::Increment(PerformanceMetrics::Counter::ReplacementPassAttempted);
				executionAttempted = true;
				if (state.replacementPassActive)
				{
					HookD3D12::ScopedRenderPassInjection injectionScope;
					commandList->SetPipelineState(state.boundPipelineState);
					ShaderResourceRuntime::RestoreResources(commandList);
					state.replacementPassActive = false;
				}
				captureShaderResourceState();
				bool shaderResourcesBound = false;
				{
					// Descriptor heaps and root tables installed by an injected pass are
					// temporary. Keep the hook-side game-state mirror on the bindings that
					// were active before this pass so later render passes restore correctly.
					HookD3D12::ScopedRenderPassInjection injectionScope;
					shaderResourcesBound = ShaderResourceRuntime::BindResources(
						renderPass,
						commandList,
						gShaderResourceState,
						gOppositeShaderResourceState,
						computePipeline,
						executionError);
				}
				if (!shaderResourcesBound)
				{
					executionSucceeded = false;
				}
				else
				{
					PerformanceMetrics::ScopedTimer replacementTimer(
						PerformanceMetrics::Timing::ResolveReplacementPipeline);
					ID3D12PipelineState* replacementPipeline = RenderPassReplacement::GetOrCreatePipeline(
						renderPass,
						state.pipelineState,
						executionError);
					if (replacementPipeline)
					{
						HookD3D12::ScopedRenderPassInjection injectionScope;
						commandList->SetPipelineState(replacementPipeline);
						state.replacementPassActive = true;
						executionSucceeded = true;
						PerformanceMetrics::Increment(PerformanceMetrics::Counter::ReplacementPassSucceeded);
					}
					else
					{
						HookD3D12::ScopedRenderPassInjection injectionScope;
						ShaderResourceRuntime::RestoreResources(commandList);
					}
				}
			}
			else if (computePipeline &&
				renderPass.type == RenderPass::RenderPassType::Custom &&
				RenderPass::ResolveExecutionMode(renderPass) == RenderPass::ExecutionMode::Compute &&
				(passOperation == RenderPass::PassOperation::Custom ||
					passOperation == RenderPass::PassOperation::Downsample ||
					passOperation == RenderPass::PassOperation::UpsampleChain) &&
				RenderPass::HasCompiledShaders(renderPass))
			{
				PerformanceMetrics::Increment(PerformanceMetrics::Counter::CustomPassAttempted);
				executionAttempted = true;
				captureShaderResourceState();

				thread_local std::vector<RenderPassTexturePool::TextureView> unorderedAccessOutputs;
				unorderedAccessOutputs.clear();
				if (passOperation == RenderPass::PassOperation::UpsampleChain)
				{
					const RenderPass::LogicalResourceBindingDisk* sourceBinding =
						FindRuntimeInput(renderPass, RenderPass::ResourceAccess::ShaderResource);
					const RenderPass::LogicalResourceBindingDisk* destinationBinding =
						FindRuntimeOutput(renderPass, RenderPass::ResourceAccess::UnorderedAccess);
					RenderPassTexturePool::TextureView sourceTexture;
					RenderPassTexturePool::TextureView destinationTexture;
					thread_local std::vector<RenderPassTexturePool::TextureView> stageTargets;
					stageTargets.clear();
					if (ResolveRuntimeTexture(sourceBinding, sourceTexture, executionError, "upsample source", true) &&
						ResolveRuntimeTexture(destinationBinding, destinationTexture, executionError, "upsample destination") &&
						RenderPassTexturePool::EnsureUpsampleChainStages(
							renderPass,
							commandList,
							sourceTexture,
							destinationTexture,
							true,
							stageTargets,
							executionError))
					{
						ID3D12PipelineState* computePipelineState = RenderPassReplacement::GetOrCreatePipeline(
							renderPass,
							state.pipelineState,
							executionError);
						executionSucceeded = computePipelineState != nullptr;
						RenderPassTexturePool::TextureView stageSource = sourceTexture;
						for (const RenderPassTexturePool::TextureView& stageTarget : stageTargets)
						{
							const ShaderResourceRuntime::RuntimeTextureBindingOverride textureOverride{
								sourceBinding->resourceId,
								stageSource.shaderResourceView,
								destinationBinding->resourceId,
								stageTarget.unorderedAccessView };
							bool resourcesBound = false;
							{
								HookD3D12::ScopedRenderPassInjection injectionScope;
								resourcesBound = ShaderResourceRuntime::BindResources(
									renderPass,
									commandList,
									gShaderResourceState,
									gOppositeShaderResourceState,
									true,
									executionError,
									&textureOverride);
							}
							unorderedAccessOutputs.assign(1, stageTarget);
							UINT threadGroupCountX = 0;
							UINT threadGroupCountY = 0;
							UINT threadGroupCountZ = 0;
							if (resourcesBound && ResolveComputeDispatch(
								renderPass,
								originalThreadGroupCountX,
								originalThreadGroupCountY,
								originalThreadGroupCountZ,
								unorderedAccessOutputs,
								threadGroupCountX,
								threadGroupCountY,
								threadGroupCountZ,
								executionError))
							{
								executionSucceeded = RenderPassExecutor::ExecuteCompute(
									renderPass,
									commandList,
									computePipelineState,
									state.boundPipelineState,
									threadGroupCountX,
									threadGroupCountY,
									threadGroupCountZ,
									unorderedAccessOutputs,
									executionError);
							}
							else
							{
								executionSucceeded = false;
							}
							{
								HookD3D12::ScopedRenderPassInjection injectionScope;
								ShaderResourceRuntime::RestoreResources(commandList);
							}
							if (!executionSucceeded)
								break;
							stageSource = stageTarget;
						}
					}
				}
				else
				{
					for (const RenderPass::LogicalResourceBindingDisk& output : renderPass.outputs)
					{
						if (output.origin != ShaderResource::ResourceOrigin::Runtime ||
							output.access != RenderPass::ResourceAccess::UnorderedAccess)
							continue;
						RenderPassTexturePool::TextureView texture;
						if (!RenderPassTexturePool::GetTexture(output.resourceId, output.temporalView, texture) ||
							!texture.unorderedAccessView.ptr)
						{
							if (output.optional)
								continue;
							executionError = "Runtime unordered-access output is unavailable: " + output.resourceId;
							break;
						}
						const bool duplicate = std::any_of(
							unorderedAccessOutputs.begin(),
							unorderedAccessOutputs.end(),
							[&](const auto& existing) { return existing.resource.Get() == texture.resource.Get(); });
						if (!duplicate)
							unorderedAccessOutputs.push_back(std::move(texture));
					}

					bool shaderResourcesBound = false;
					if (executionError.empty())
					{
						HookD3D12::ScopedRenderPassInjection injectionScope;
						shaderResourcesBound = ShaderResourceRuntime::BindResources(
							renderPass,
							commandList,
							gShaderResourceState,
							gOppositeShaderResourceState,
							true,
							executionError);
					}

					ID3D12PipelineState* computePipelineState = shaderResourcesBound
						? RenderPassReplacement::GetOrCreatePipeline(renderPass, state.pipelineState, executionError)
						: nullptr;
					UINT threadGroupCountX = 0;
					UINT threadGroupCountY = 0;
					UINT threadGroupCountZ = 0;
					if (computePipelineState && ResolveComputeDispatch(
						renderPass,
						originalThreadGroupCountX,
						originalThreadGroupCountY,
						originalThreadGroupCountZ,
						unorderedAccessOutputs,
						threadGroupCountX,
						threadGroupCountY,
						threadGroupCountZ,
						executionError))
					{
						executionSucceeded = RenderPassExecutor::ExecuteCompute(
							renderPass,
							commandList,
							computePipelineState,
							state.boundPipelineState,
							threadGroupCountX,
							threadGroupCountY,
							threadGroupCountZ,
							unorderedAccessOutputs,
							executionError);
					}
					{
						HookD3D12::ScopedRenderPassInjection injectionScope;
						ShaderResourceRuntime::RestoreResources(commandList);
					}
				}
				if (executionSucceeded)
					PerformanceMetrics::Increment(PerformanceMetrics::Counter::CustomPassSucceeded);
			}
			else if (!computePipeline && RenderPass::HasCompiledShaders(renderPass) &&
				(passOperation == RenderPass::PassOperation::Custom ||
				passOperation == RenderPass::PassOperation::Downsample ||
				passOperation == RenderPass::PassOperation::UpsampleChain))
			{
				PerformanceMetrics::Increment(PerformanceMetrics::Counter::CustomPassAttempted);
				thread_local std::vector<RenderPass::ResourceBindingDiagnostic> effectiveOutputBindings;
				effectiveOutputBindings.assign(state.outputBindings.begin(), state.outputBindings.end());
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
				captureShaderResourceState();
				captureFullscreenGraphicsState();
				if (passOperation == RenderPass::PassOperation::UpsampleChain)
				{
					const RenderPass::LogicalResourceBindingDisk* sourceBinding =
						FindRuntimeInput(renderPass, RenderPass::ResourceAccess::ShaderResource);
					RenderPassTexturePool::TextureView sourceTexture;
					RenderPassTexturePool::TextureView destinationTexture;
					thread_local std::vector<RenderPassTexturePool::TextureView> stageTargets;
					stageTargets.clear();
					if (ResolveRuntimeTexture(sourceBinding, sourceTexture, executionError, "upsample source", true) &&
						ResolveRuntimeTexture(
							FindRuntimeOutput(renderPass, RenderPass::ResourceAccess::RenderTarget),
							destinationTexture,
							executionError,
							"upsample destination") &&
						RenderPassTexturePool::EnsureUpsampleChainStages(
							renderPass,
							commandList,
							sourceTexture,
							destinationTexture,
							false,
							stageTargets,
							executionError))
					{
						executionSucceeded = true;
						RenderPassTexturePool::TextureView stageSource = sourceTexture;
						for (const RenderPassTexturePool::TextureView& stageTarget : stageTargets)
						{
							const ShaderResourceRuntime::RuntimeTextureBindingOverride sourceOverride{
								sourceBinding->resourceId,
								stageSource.shaderResourceView };
							bool resourcesBound = false;
							{
								HookD3D12::ScopedRenderPassInjection injectionScope;
								resourcesBound = ShaderResourceRuntime::BindResources(
									renderPass,
									commandList,
									gShaderResourceState,
									gOppositeShaderResourceState,
									false,
									executionError,
									&sourceOverride);
							}
							if (resourcesBound)
							{
								executionSucceeded = RenderPassExecutor::ExecuteFullscreenTriangle(
									renderPass,
									commandList,
									state.graphicsRootSignature,
									state.boundPipelineState,
									state.primitiveTopology,
									effectiveOutputBindings,
									&stageTarget,
									&gFullscreenGraphicsState,
									executionError);
							}
							else
							{
								executionSucceeded = false;
							}
							{
								HookD3D12::ScopedRenderPassInjection injectionScope;
								ShaderResourceRuntime::RestoreResources(commandList);
							}
							if (!executionSucceeded)
								break;
							stageSource = stageTarget;
						}
					}
				}
				else
				{
					RenderPassTexturePool::TextureView runtimeOutputTexture;
					const RenderPass::LogicalResourceBindingDisk* outputBinding =
						FindRuntimeOutput(renderPass, RenderPass::ResourceAccess::RenderTarget);
					const RenderPassTexturePool::TextureView* runtimeOutput = nullptr;
					bool outputAvailable = true;
					if (outputBinding)
					{
						outputAvailable = ResolveRuntimeTexture(
							outputBinding,
							runtimeOutputTexture,
							executionError,
							"render target");
						runtimeOutput = outputAvailable ? &runtimeOutputTexture : nullptr;
					}

					bool shaderResourcesBound = false;
					if (outputAvailable)
					{
						HookD3D12::ScopedRenderPassInjection injectionScope;
						shaderResourcesBound = ShaderResourceRuntime::BindResources(
							renderPass,
							commandList,
							gShaderResourceState,
							gOppositeShaderResourceState,
							false,
							executionError);
					}
					if (!shaderResourcesBound)
					{
						executionSucceeded = false;
					}
					else
					{
						PerformanceMetrics::ScopedTimer customPassTimer(
							PerformanceMetrics::Timing::ExecuteCustomPass);
						executionSucceeded = RenderPassExecutor::ExecuteFullscreenTriangle(
							renderPass,
							commandList,
							state.graphicsRootSignature,
							state.boundPipelineState,
							state.primitiveTopology,
							effectiveOutputBindings,
							runtimeOutput,
							runtimeOutput ? &gFullscreenGraphicsState : nullptr,
							executionError);
					}
					if (executionSucceeded)
						PerformanceMetrics::Increment(PerformanceMetrics::Counter::CustomPassSucceeded);
					{
						HookD3D12::ScopedRenderPassInjection injectionScope;
						ShaderResourceRuntime::RestoreResources(commandList);
					}
				}
			}

			if (mipRuntimeSource)
			{
				const std::string outputId = RenderPass::MipChainOutputResourceId(renderPass);
				RenderPassTexturePool::OverrideInputTexture(
					mipRuntimeSource->resourceId, mipRuntimeSource->temporalView, generatedMipTexture);
				RenderPassTexturePool::OverrideInputTexture(
					outputId, ShaderResource::TemporalView::Current, generatedMipTexture);
				if (executionSucceeded)
				{
					unavailableRuntimeResources.erase(mipRuntimeSource->resourceId);
					unavailableRuntimeResources.erase(outputId);
				}
				else
				{
					unavailableRuntimeResources.insert(mipRuntimeSource->resourceId);
					unavailableRuntimeResources.insert(outputId);
				}
			}
			for (const RenderPass::LogicalResourceBindingDisk& output : renderPass.outputs)
			{
				if (output.origin != ShaderResource::ResourceOrigin::Runtime || output.resourceId.empty())
					continue;
				if (executionSucceeded)
					unavailableRuntimeResources.erase(output.resourceId);
				else
					unavailableRuntimeResources.insert(output.resourceId);
			}

			const size_t renderPassIndex = static_cast<size_t>(
				renderPassPointer - configuration->renderPasses.data());
			RuntimeCounters& counters = *configuration->runtimeCounters[renderPassIndex];
			const bool firstTrigger =
				counters.triggerCount.fetch_add(1, std::memory_order_relaxed) == 0;
			const bool firstSuccessfulExecution = executionSucceeded &&
				counters.executionCount.fetch_add(1, std::memory_order_relaxed) == 0;
			const bool firstFailedExecution = !executionSucceeded && executionAttempted &&
				counters.executionFailureCount.fetch_add(1, std::memory_order_relaxed) == 0;
			bool executionStateChanged = false;
			if (executionSucceeded || executionAttempted)
			{
				const uint8_t resultState = executionSucceeded ? 1u : 2u;
				executionStateChanged =
					counters.lastExecutionResult.exchange(resultState, std::memory_order_relaxed) != resultState;
			}
			bool captureResourceSnapshot = false;
			const bool resourceSnapshotPending = renderPass.trackResourceBindings &&
				gResourceTrackingRequired.load(std::memory_order_relaxed);
			if (firstTrigger || executionStateChanged || resourceSnapshotPending)
			{
				std::lock_guard<std::mutex> lock(gDiagnosticsMutex);
				RenderPass::RuntimeDiagnostics& diagnostics = gDiagnosticsByRenderPassId[renderPass.id];
				const bool targetChanged = diagnostics.lastModifiedShaderId != targetIt->second.modifiedShaderId ||
					diagnostics.lastShaderTargetName != targetIt->second.name;
				if (firstTrigger || targetChanged || executionStateChanged)
				{
					diagnostics.lastTiming = renderPass.timing;
					diagnostics.lastOperation = operationName ? operationName : "Unknown";
					diagnostics.lastExecutionError = executionError;
					diagnostics.lastEventType = RenderPass::EventTypeName(renderPass.event.type);
					diagnostics.lastEventId = renderPass.event.id;
					diagnostics.lastModifiedShaderId = resolvedEvent->modifiedShaderId;
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
						RefreshTrackingModeFlags();
					}
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
					"RenderPassRuntime->RecordExecutionBoundary: matched pass=%s type=%s event=%s:%s timing=%s rootBoundary=%s operation=%s commandList=%p requestedPSO=%p boundPSO=%p rootSignature=%p outputs=%llu pipelineRTVs=%u format0=%u samples=%u/%u compiled=%u mipSource=t%u,space%u",
					renderPass.name.c_str(),
					RenderPass::TypeName(renderPass.type),
					RenderPass::EventTypeName(renderPass.event.type),
					renderPass.event.id.c_str(),
					renderPass.timing.c_str(),
					boundary == ExecutionBoundary::Before ? RenderPass::timingBefore : RenderPass::timingAfter,
					operationName ? operationName : "Unknown",
					commandList,
					state.pipelineState,
					state.boundPipelineState,
					computePipeline ? state.computeRootSignature : state.graphicsRootSignature,
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
				if (shaderResourceStateBuilt &&
					(renderPass.inheritedGameBindings.shaderResources ||
						renderPass.inheritedGameBindings.constantBuffers))
				{
					size_t descriptorTableCount = 0;
					size_t constantRootArgumentCount = 0;
					size_t resourceRootArgumentCount = 0;
					for (const RenderPassMipChain::RootArgumentSnapshot& binding : gShaderResourceState.rootBindings)
					{
						if (binding.type == RenderPassMipChain::RootArgumentType::DescriptorTable)
							++descriptorTableCount;
						else if (binding.type == RenderPassMipChain::RootArgumentType::ConstantBufferView ||
							binding.type == RenderPassMipChain::RootArgumentType::Constants)
							++constantRootArgumentCount;
						else
							++resourceRootArgumentCount;
					}
					ShaderInjectorIO::WriteToLogFile(StringHelper::Format(
						"RenderPassRuntime->RecordExecutionBoundary: inherited bindings pass=%s tables=%llu constantRoots=%llu resourceRoots=%llu inheritResources=%u inheritConstants=%u",
						renderPass.name.c_str(),
						static_cast<unsigned long long>(descriptorTableCount),
						static_cast<unsigned long long>(constantRootArgumentCount),
						static_cast<unsigned long long>(resourceRootArgumentCount),
						renderPass.inheritedGameBindings.shaderResources ? 1u : 0u,
						renderPass.inheritedGameBindings.constantBuffers ? 1u : 0u));
				}
				if (mipRuntimeSource)
				{
					ShaderInjectorIO::WriteToLogFileSuccess(StringHelper::Format(
						"RenderPassRuntime->RecordExecutionBoundary: runtime mip chain source=%s output=%s extent=%ux%u mips=%u mode=%s",
						mipRuntimeSource->resourceId.c_str(),
						RenderPass::MipChainOutputResourceId(renderPass).c_str(),
						generatedMipTexture.description.width,
						generatedMipTexture.description.height,
						generatedMipTexture.description.mipLevels,
						RenderPass::ExecutionModeName(RenderPass::ResolveExecutionMode(renderPass))));
				}
			}
			else if (firstFailedExecution)
			{
				ShaderInjectorGUI::WriteToRuntimeLogError(
					"RenderPassRuntime->RecordExecutionBoundary: first render-pass execution failed for " +
					renderPass.name + " via " + (operationName ? operationName : "Unknown") +
					" error=" + executionError);
			}
		}
	}

	void CompleteGraphicsExecutionBoundary(ID3D12GraphicsCommandList* commandList)
	{
		CommandListRenderState& state = GetCommandListState(commandList);
		if (state.replacementPassActive)
		{
			HookD3D12::ScopedRenderPassInjection injectionScope;
			commandList->SetPipelineState(state.boundPipelineState);
			ShaderResourceRuntime::RestoreResources(commandList);
			state.replacementPassActive = false;
		}
		if (gPendingMipRestore && gPendingMipRestoreCommandList == commandList)
		{
			PerformanceMetrics::ScopedTimer restoreTimer(
				PerformanceMetrics::Timing::RestoreMipState);
			RenderPassMipChain::RestoreAfterTargetDraw(commandList, gMipChainGraphicsState, false);
			gPendingMipRestore = false;
			gPendingMipRestoreCommandList = nullptr;
		}
	}

	void CompleteComputeExecutionBoundary(ID3D12GraphicsCommandList* commandList)
	{
		CommandListRenderState& state = GetCommandListState(commandList);
		if (state.replacementPassActive)
		{
			HookD3D12::ScopedRenderPassInjection injectionScope;
			commandList->SetPipelineState(state.boundPipelineState);
			ShaderResourceRuntime::RestoreResources(commandList);
			state.replacementPassActive = false;
		}
		if (gPendingMipRestore && gPendingMipRestoreCommandList == commandList)
		{
			PerformanceMetrics::ScopedTimer restoreTimer(
				PerformanceMetrics::Timing::RestoreMipState);
			RenderPassMipChain::RestoreAfterTargetDraw(commandList, gMipChainGraphicsState, true);
			gPendingMipRestore = false;
			gPendingMipRestoreCommandList = nullptr;
		}
	}

	void NotifyCommandListsSubmitted(
		ID3D12CommandQueue* commandQueue,
		UINT commandListCount,
		ID3D12CommandList* const* commandLists)
	{
		PerformanceMetrics::ScopedTimer submissionTimer(
			PerformanceMetrics::Timing::RetireMipSubmissions,
			16);
		RenderPassMipChain::NotifyCommandListsSubmitted(
			commandQueue,
			commandListCount,
			commandLists);
		ShaderResourceRuntime::NotifyCommandListsSubmitted(
			commandQueue,
			commandListCount,
			commandLists);
	}

	void AdvanceFrame()
	{
		RenderPassTexturePool::AdvanceFrame();
	}

	void LogPerformanceSnapshot()
	{
		if (!Globals::gPerformanceTelemetryEnabled)
			return;

		const RenderPassResourceRegistry::RegistryStatistics registryStatistics =
			RenderPassResourceRegistry::GetStatistics();
		ShaderInjectorIO::WriteToLogFile(StringHelper::Format(
			"RenderPassResourceRegistry->Performance: descriptors=%llu descriptorHeaps=%llu retiredDescriptorHeaps=%llu heapDescriptors=%llu fallbackDescriptors=%llu metadataRecords=%llu bufferResources=%llu rootSignatures=%llu",
			static_cast<unsigned long long>(registryStatistics.descriptorCount),
			static_cast<unsigned long long>(registryStatistics.descriptorHeapCount),
			static_cast<unsigned long long>(registryStatistics.retiredDescriptorHeapCount),
			static_cast<unsigned long long>(registryStatistics.heapDescriptorCount),
			static_cast<unsigned long long>(registryStatistics.fallbackDescriptorCount),
			static_cast<unsigned long long>(registryStatistics.descriptorMetadataCount),
			static_cast<unsigned long long>(registryStatistics.bufferResourceCount),
			static_cast<unsigned long long>(registryStatistics.rootSignatureCount)));

		const RenderPassConfigurationSnapshot* configuration =
			gPublishedConfiguration.load(std::memory_order_acquire);
		if (!configuration)
			return;

		for (size_t renderPassIndex = 0;
			renderPassIndex < configuration->renderPasses.size() &&
			renderPassIndex < configuration->runtimeCounters.size();
			++renderPassIndex)
		{
			const RenderPass::RenderPassDisk& renderPass = configuration->renderPasses[renderPassIndex];
			RuntimeCounters& counters = *configuration->runtimeCounters[renderPassIndex];
			const uint64_t triggerCount = counters.triggerCount.load(std::memory_order_relaxed);
			const uint64_t executionCount = counters.executionCount.load(std::memory_order_relaxed);
			const uint64_t failureCount = counters.executionFailureCount.load(std::memory_order_relaxed);
			const uint64_t previousTriggers = counters.reportedTriggerCount.exchange(
				triggerCount,
				std::memory_order_relaxed);
			const uint64_t previousExecutions = counters.reportedExecutionCount.exchange(
				executionCount,
				std::memory_order_relaxed);
			const uint64_t previousFailures = counters.reportedFailureCount.exchange(
				failureCount,
				std::memory_order_relaxed);
			const uint64_t triggerDelta = triggerCount - previousTriggers;
			const uint64_t executionDelta = executionCount - previousExecutions;
			const uint64_t failureDelta = failureCount - previousFailures;
			if (!triggerDelta && !executionDelta && !failureDelta)
				continue;

			ShaderInjectorIO::WriteToLogFile(StringHelper::Format(
				"RenderPassRuntime->Performance: pass=%s type=%s enabled=%u triggers=%llu successes=%llu failures=%llu",
				renderPass.name.c_str(),
				RenderPass::TypeName(renderPass.type),
				renderPass.enabled ? 1u : 0u,
				static_cast<unsigned long long>(triggerDelta),
				static_cast<unsigned long long>(executionDelta),
				static_cast<unsigned long long>(failureDelta)));
		}
	}

	RenderPass::RuntimeDiagnostics GetDiagnostics(const std::string& renderPassId)
	{
		RenderPass::RuntimeDiagnostics diagnostics{};
		{
			std::lock_guard<std::mutex> lock(gDiagnosticsMutex);
			const auto diagnosticsIt = gDiagnosticsByRenderPassId.find(renderPassId);
			if (diagnosticsIt != gDiagnosticsByRenderPassId.end())
				diagnostics = diagnosticsIt->second;
		}

		const RenderPassConfigurationSnapshot* configuration =
			gPublishedConfiguration.load(std::memory_order_acquire);
		const auto renderPassIt = configuration->renderPassIndices.find(renderPassId);
		if (renderPassIt != configuration->renderPassIndices.end() &&
			renderPassIt->second < configuration->runtimeCounters.size())
		{
			const RuntimeCounters& counters = *configuration->runtimeCounters[renderPassIt->second];
			diagnostics.triggerCount = counters.triggerCount.load(std::memory_order_relaxed);
			diagnostics.executionCount = counters.executionCount.load(std::memory_order_relaxed);
			diagnostics.executionFailureCount =
				counters.executionFailureCount.load(std::memory_order_relaxed);
		}
		return diagnostics;
	}

	void ClearDiagnostics(const std::string& renderPassId)
	{
		const RenderPassConfigurationSnapshot* configuration =
			gPublishedConfiguration.load(std::memory_order_acquire);
		const ShaderTargetBindingMap* shaderTargetBindings =
			gPublishedShaderTargetBindings.load(std::memory_order_acquire);
		const auto renderPassIt = configuration->renderPassIndices.find(renderPassId);
		if (renderPassIt != configuration->renderPassIndices.end() &&
			renderPassIt->second < configuration->runtimeCounters.size())
		{
			RuntimeCounters& counters = *configuration->runtimeCounters[renderPassIt->second];
			counters.triggerCount.store(0, std::memory_order_relaxed);
			counters.executionCount.store(0, std::memory_order_relaxed);
			counters.executionFailureCount.store(0, std::memory_order_relaxed);
			counters.reportedTriggerCount.store(0, std::memory_order_relaxed);
			counters.reportedExecutionCount.store(0, std::memory_order_relaxed);
			counters.reportedFailureCount.store(0, std::memory_order_relaxed);
			counters.lastExecutionResult.store(0, std::memory_order_relaxed);
		}

		std::lock_guard<std::mutex> lock(gDiagnosticsMutex);
		gDiagnosticsByRenderPassId.erase(renderPassId);
		for (const RenderPass::RenderPassDisk& renderPass : configuration->renderPasses)
		{
			if (renderPass.id == renderPassId && renderPass.enabled && renderPass.trackResourceBindings &&
				HasLinkedShaderTargetBinding(*configuration, renderPass, *shaderTargetBindings))
			{
				gPendingResourceSnapshotIds.insert(renderPassId);
				break;
			}
		}
		gResourceTrackingRequired.store(!gPendingResourceSnapshotIds.empty(), std::memory_order_release);
		RefreshTrackingModeFlags();
	}
}
