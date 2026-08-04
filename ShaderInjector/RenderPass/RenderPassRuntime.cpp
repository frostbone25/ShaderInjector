#include "RenderPassRuntime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
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
		};

		struct RenderPassConfigurationSnapshot
		{
			std::vector<RenderPass::RenderPassDisk> renderPasses;
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

		std::mutex gDiagnosticsMutex;
		std::unordered_map<std::string, RenderPass::RuntimeDiagnostics> gDiagnosticsByRenderPassId;
		std::unordered_set<std::string> gPendingResourceSnapshotIds;

		enum TrackingModeFlag : uint32_t
		{
			TrackingEnabled = 1u << 0,
			ResourceTrackingEnabled = 1u << 1,
			DescriptorRegistryTrackingEnabled = 1u << 2,
			GraphicsStateTrackingEnabled = 1u << 3
		};

		void RefreshTrackingModeFlags()
		{
			const bool trackingEnabled =
				gHasEnabledRenderPasses.load(std::memory_order_relaxed) &&
				gHasExecutableRenderPassBinding.load(std::memory_order_relaxed);
			const bool resourceTrackingEnabled = trackingEnabled &&
				gResourceTrackingRequired.load(std::memory_order_relaxed);
			const bool descriptorRegistryTrackingEnabled = resourceTrackingEnabled ||
				(trackingEnabled && gHasExecutableMipChainBinding.load(std::memory_order_relaxed));

			uint32_t flags = trackingEnabled ? TrackingEnabled : 0;
			if (resourceTrackingEnabled)
				flags |= ResourceTrackingEnabled;
			if (descriptorRegistryTrackingEnabled)
				flags |= DescriptorRegistryTrackingEnabled | GraphicsStateTrackingEnabled;
			gTrackingModeFlags.store(flags, std::memory_order_release);
		}

		void BuildResolvedEventBindings(RenderPassConfigurationSnapshot& configuration)
		{
			configuration.renderPassIndices.clear();
			configuration.resolvedEvents.assign(configuration.renderPasses.size(), {});
			for (size_t renderPassIndex = 0; renderPassIndex < configuration.renderPasses.size(); ++renderPassIndex)
				configuration.renderPassIndices[configuration.renderPasses[renderPassIndex].id] = renderPassIndex;

			enum class ResolutionState : uint8_t
			{
				Unvisited,
				Visiting,
				Complete,
			};
			std::vector<ResolutionState> resolutionStates(
				configuration.renderPasses.size(),
				ResolutionState::Unvisited);

			const auto resolveEvent = [&](const auto& resolveEventSelf, size_t renderPassIndex) -> ResolvedEventBinding
			{
				if (resolutionStates[renderPassIndex] == ResolutionState::Complete)
					return configuration.resolvedEvents[renderPassIndex];
				if (resolutionStates[renderPassIndex] == ResolutionState::Visiting)
					return {};

				resolutionStates[renderPassIndex] = ResolutionState::Visiting;
				const RenderPass::RenderPassDisk& renderPass = configuration.renderPasses[renderPassIndex];
				ResolvedEventBinding resolved{};
				if (renderPass.enabled && !renderPass.event.id.empty())
				{
					if (renderPass.event.type == RenderPass::EventType::ModifiedShader)
					{
						resolved.valid = true;
						resolved.modifiedShaderId = renderPass.event.id;
						resolved.rootBoundary = renderPass.type == RenderPass::RenderPassType::MipChain ||
							renderPass.timing != RenderPass::timingAfter
							? ExecutionBoundary::Before
							: ExecutionBoundary::After;
					}
					else
					{
						const auto parentIt = configuration.renderPassIndices.find(renderPass.event.id);
						if (parentIt != configuration.renderPassIndices.end() && parentIt->second != renderPassIndex)
							resolved = resolveEventSelf(resolveEventSelf, parentIt->second);
					}
				}

				// A mip chain modifies descriptor bindings for the game draw that follows it.
				// It cannot safely live on a graph rooted after that draw has already executed.
				if (resolved.valid && renderPass.type == RenderPass::RenderPassType::MipChain &&
					resolved.rootBoundary == ExecutionBoundary::After)
				{
					resolved = {};
				}

				configuration.resolvedEvents[renderPassIndex] = resolved;
				resolutionStates[renderPassIndex] = ResolutionState::Complete;
				return resolved;
			};

			for (size_t renderPassIndex = 0; renderPassIndex < configuration.renderPasses.size(); ++renderPassIndex)
				resolveEvent(resolveEvent, renderPassIndex);
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
					snapshot.rootBindings.push_back(BuildRootBindingDiagnostic(
						rootBinding,
						rootParameterIndex,
						false));
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

		void AppendRenderPassExecutionOrder(
			const RenderPassConfigurationSnapshot& configuration,
			size_t renderPassIndex,
			std::vector<const RenderPass::RenderPassDisk*>& executionOrder,
			std::vector<uint8_t>& appendedRenderPasses)
		{
			if (renderPassIndex >= configuration.renderPasses.size() || appendedRenderPasses[renderPassIndex])
				return;
			const RenderPass::RenderPassDisk& renderPass = configuration.renderPasses[renderPassIndex];
			if (!FindResolvedEventBinding(configuration, renderPass))
			{
				return;
			}
			appendedRenderPasses[renderPassIndex] = 1;

			const auto appendChildren = [&](const char* timing)
			{
				for (size_t childIndex = 0; childIndex < configuration.renderPasses.size(); ++childIndex)
				{
					const RenderPass::RenderPassDisk& child = configuration.renderPasses[childIndex];
					if (child.enabled && child.event.type == RenderPass::EventType::RenderPass &&
						child.event.id == renderPass.id && child.timing == timing)
					{
						AppendRenderPassExecutionOrder(
							configuration,
							childIndex,
							executionOrder,
							appendedRenderPasses);
					}
				}
			};

			appendChildren(RenderPass::timingBefore);
			executionOrder.push_back(&renderPass);
			appendChildren(RenderPass::timingAfter);
		}

		void BuildModifiedShaderExecutionPlans(RenderPassConfigurationSnapshot& configuration)
		{
			configuration.executionPlans.clear();
			for (size_t renderPassIndex = 0; renderPassIndex < configuration.renderPasses.size(); ++renderPassIndex)
			{
				const RenderPass::RenderPassDisk& renderPass = configuration.renderPasses[renderPassIndex];
				const ResolvedEventBinding* resolvedEvent =
					FindResolvedEventBinding(configuration, renderPass);
				if (!resolvedEvent || renderPass.event.type != RenderPass::EventType::ModifiedShader)
				{
					continue;
				}

				ModifiedShaderExecutionPlan& plan = configuration.executionPlans[resolvedEvent->modifiedShaderId];
				const size_t boundaryIndex = resolvedEvent->rootBoundary == ExecutionBoundary::After ? 1u : 0u;
				std::vector<const RenderPass::RenderPassDisk*> rootExecutionOrder;
				rootExecutionOrder.reserve(configuration.renderPasses.size());
				std::vector<uint8_t> appendedRenderPasses(configuration.renderPasses.size(), 0);
				AppendRenderPassExecutionOrder(
					configuration,
					renderPassIndex,
					rootExecutionOrder,
					appendedRenderPasses);
				plan.executionOrders[boundaryIndex].insert(
					plan.executionOrders[boundaryIndex].end(),
					rootExecutionOrder.begin(),
					rootExecutionOrder.end());
				for (const RenderPass::RenderPassDisk* candidate : rootExecutionOrder)
				{
					if (candidate && candidate->type == RenderPass::RenderPassType::MipChain)
						plan.mipChainOrders[boundaryIndex].push_back(candidate);
				}
				const uint32_t boundaryMask = boundaryIndex == 1 ? 2u : 1u;
				plan.graphicsBoundaryMask |= boundaryMask;
				if (std::any_of(
					rootExecutionOrder.begin(),
					rootExecutionOrder.end(),
					[](const RenderPass::RenderPassDisk* candidate)
					{
						return candidate && candidate->type != RenderPass::RenderPassType::MipChain;
					}))
				{
					plan.computeBoundaryMask |= boundaryMask;
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
		auto snapshot = std::make_unique<RenderPassConfigurationSnapshot>();
		snapshot->renderPasses = renderPasses;
		BuildResolvedEventBindings(*snapshot);
		BuildModifiedShaderExecutionPlans(*snapshot);
		bool hasEnabledRenderPass = false;
		bool hasEnabledMipChainPass = false;
		std::unordered_set<std::string> activeIds;

		for (const RenderPass::RenderPassDisk& renderPass : renderPasses)
		{
			activeIds.insert(renderPass.id);
			if (renderPass.enabled && !renderPass.event.id.empty())
			{
				hasEnabledRenderPass = true;
				hasEnabledMipChainPass = hasEnabledMipChainPass ||
					renderPass.type == RenderPass::RenderPassType::MipChain;
				if (!FindResolvedEventBinding(*snapshot, renderPass))
				{
					ShaderInjectorIO::WriteToLogFileWarning(
						"RenderPassRuntime->PublishRenderPassConfigurations: unresolved or invalid event chain for " +
						renderPass.name + " event=" + RenderPass::EventTypeName(renderPass.event.type) +
						":" + renderPass.event.id);
				}
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
			RenderPassMipChain::ResetCommandListRecording(commandList);
	}

	void TrackPipelineState(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* pipelineState)
	{
		if (!IsTrackingRequired())
			return;

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

		std::vector<RenderPassMipChain::ExecutionResult> mipChainResults;
		bool mipChainsPrepared = false;
		thread_local RenderPassMipChain::GraphicsStateSnapshot mipChainGraphicsState;

		for (const RenderPass::RenderPassDisk* renderPassPointer : executionOrder)
		{
			const RenderPass::RenderPassDisk& renderPass = *renderPassPointer;
			const ResolvedEventBinding* resolvedEvent =
				FindResolvedEventBinding(*configuration, renderPass);
			if (!resolvedEvent ||
				(renderPass.type == RenderPass::RenderPassType::MipChain && computePipeline))
			{
				continue;
			}

			bool executionAttempted = false;
			bool executionSucceeded = false;
			std::string executionError;
			if (renderPass.type == RenderPass::RenderPassType::MipChain)
			{
				if (!mipChainsPrepared)
				{
					BuildMipChainGraphicsState(state, mipChainGraphicsState);
					mipChainResults = RenderPassMipChain::PrepareForTargetDraw(
						executionPlan->mipChainOrders[boundaryIndex],
						commandList,
						mipChainGraphicsState);
					mipChainsPrepared = true;
				}

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
