//d3d12 hook implementation
#include "Enum/ShaderTargetApplyResult.h"
#include <windows.h>
#include <wrl/client.h>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstdarg>
#include <unordered_set>
#include <map>
#include <unordered_map>
#include <Psapi.h>
#include <string>
#include <mutex>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <deque>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <dxgi1_6.h>
#include <d3d9.h>
#include <d3d10_1.h>
#include <d3d10.h>
#include <d3d11.h>
#include <d3d12.h>

//minhook
#include "MinHook.h"

//imgui
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

//project headers
#include "Hooks.h"
#include "Globals.h"
#include "dsound_proxy.h"
#include "HookD3D12.h"
#include "ShaderTarget/DatabaseShaderTargets.h"
#include "HookInput.h"
#include "IO/ShaderInjectorIO.h"
#include "ShaderTarget/ShaderTarget.h"
#include "GUI/ShaderInjectorGUI.h"
#include "Hash/Hash.h"
#include "FPSCounter.h"
#include "ShaderAutomaticDiscovery.h"
#include "RenderPass/RenderPassRuntime.h"
#include "RenderPass/RenderPassExecutor.h"
#include "RenderPass/RenderPassResourceRegistry.h"
#include "Performance/PerformanceMetrics.h"
#include "RenderDoc/RenderDocIntegration.h"
#include "VTableIndex.h"
#include "StringHelper.h"
#include "D3D12/HookD3D12RuntimeState.h"
#include "ThreadDescriptorIncrements.h"
#include "OverrideSnapshotReadScope.h"
#include "PipelineBindingCacheEntry.h"
#include "ShaderCandidate.h"
#include "SwapChainCommandQueueBinding.h"

namespace HookD3D12
{
	template <typename Element>
	static Element* DataOrNull(std::vector<Element>& values)
	{
		if (values.empty())
			return nullptr;

		return values.data();
	}

	template <typename Element>
	static const Element* DataOrNull(const std::vector<Element>& values)
	{
		if (values.empty())
			return nullptr;

		return values.data();
	}

	static D3D12_SHADER_BYTECODE MakeShaderBytecode(const std::vector<uint8_t>& bytecode)
	{
		return {DataOrNull(bytecode), bytecode.size()};
	}

	static const std::vector<uint8_t>& SelectPixelMarkerBlob()
	{
		if (!Globals::markerPixelShaderBlob.empty())
			return Globals::markerPixelShaderBlob;

		return Globals::nullPixelShaderBlob;
	}

	UINT DescriptorIncrementSize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType)
	{
		if (!device || heapType >= D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES)
			return 0;

		thread_local ThreadDescriptorIncrements cache;

		if (cache.device != device)
		{
			cache.device = device;
			cache.descriptorIncrementSizes.fill(0);
		}

		UINT& increment = cache.descriptorIncrementSizes[heapType];

		if (!increment)
			increment = device->GetDescriptorHandleIncrementSize(heapType);

		return increment;
	}

	void RegisterCreatedResource(HRESULT result, void** createdObject)
	{
		if (FAILED(result) || !createdObject || !*createdObject)
			return;

		ID3D12Resource* resource = nullptr;
		IUnknown* unknown = reinterpret_cast<IUnknown*>(*createdObject);

		if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&resource))) && resource)
		{
			RenderPassResourceRegistry::RegisterResource(resource);
			resource->Release();
		}
	}

	ID3D12Device* gDevice = nullptr;
	ID3D12Device* gDevice2 = nullptr;
	ID3D12CommandQueue* gCommandQueue = nullptr;
	static ID3D12CommandQueue* gMostRecentDirectCommandQueue = nullptr;
	ID3D12DescriptorHeap* gHeapRTV = nullptr;
	ID3D12DescriptorHeap* gHeapSRV = nullptr;
	ID3D12GraphicsCommandList* gCommandList = nullptr;
	ID3D12Fence* gOverlayFence = nullptr;
	HANDLE gFenceEvent = nullptr;
	UINT64 gOverlayFenceValue = 0;
	UINT gBufferCount = 0;

	FrameContext* gFrameContexts = nullptr;
	bool gInitialized = false;
	bool gShutdown = false;
	ULONGLONG gOverlayInitializedTick = 0;
	bool gLoggedStartupMenuDelay = false;
	bool gOverlayRenderingDisabled = false;
	bool gOverlayDeviceObjectsCreated = false;
	bool gLoggedPresentHook = false;
	bool gLoggedPresent1Hook = false;
	static bool gLoggedCommandQueueCaptured = false;
	static bool gLoggedExactCommandQueueCaptured = false;
	static bool gLoggedUnsafeFallbackQueue = false;
	bool gLoggedOverlayInitialized = false;
	bool gLoggedOverlayPipelineActivityDelay = false;
	UINT64 gOverlaySubmissionCount = 0;
	static std::mutex gCommandQueueCaptureMutex;
	static std::vector<SwapChainCommandQueueBinding> gSwapChainCommandQueueBindings;
	std::atomic<uint32_t> gActivePipelineActivityCount = 0;
	static DWORD gMostRecentDirectCommandQueueThreadId = 0;
	thread_local bool gInsideOverlayResourceCreation = false;

	//RTSS can install its swap-chain interception after our process-wide MinHook
	//detour. In that load order, subsequent Presents can bypass our hook entirely.
	//a private vtable for the game's swap chain lets us wrap RTSS's current targets
	//without removing RTSS from the call chain.
	static constexpr size_t gSwapChain3VTableEntryCount = 40;
	static IDXGISwapChain3* gRTSSCompatibilitySwapChain = nullptr;
	static void** gRTSSOriginalSwapChainVTable = nullptr;
	static void** gRTSSCompatibilitySwapChainVTable = nullptr;
	FunctionPresentD3D12 gRTSSOriginalPresent = nullptr;
	FunctionPresent1D3D12 gRTSSOriginalPresent1 = nullptr;
	FunctionResizeBuffersD3D12 gRTSSOriginalResizeBuffers = nullptr;
	std::mutex gRTSSCompatibilityMutex;
	thread_local bool gInsideSwapChainCompatibilityCall = false;
	std::atomic<bool> gRuntimeReady = false;

	std::vector<UncapturedPipelineStateInfo> gUncapturedPipelineStates;
	std::unordered_map<ID3D12PipelineState*, size_t> gUncapturedPipelineStateIndexByPointer;

	static std::mutex gCommandListStateRegistryMutex;
	static std::unordered_map<ID3D12GraphicsCommandList*, std::unique_ptr<CommandListPipelineState>> gCommandListStates;
	static thread_local ID3D12GraphicsCommandList* gCachedCommandList = nullptr;
	static thread_local CommandListPipelineState* gCachedCommandListState = nullptr;

	std::vector<PSOPendingRebuild> gPendingRebuilds;

	std::mutex gPipelineMutex;
	D3D12PipelineInfo gPipelineInfo;

	std::unordered_map<ID3D12PipelineState*, ID3D12PipelineState*> gPipelineStateOverrides;
	using PipelineStateOverrideMap = std::unordered_map<ID3D12PipelineState*, ID3D12PipelineState*>;
	static const PipelineStateOverrideMap gEmptyPipelineStateOverrides;
	static std::atomic<const PipelineStateOverrideMap*> gPublishedPipelineStateOverrides = &gEmptyPipelineStateOverrides;
	static std::unique_ptr<const PipelineStateOverrideMap> gOwnedPublishedPipelineStateOverrides;
	static std::vector<std::unique_ptr<const PipelineStateOverrideMap>> gRetiredPipelineStateOverrideSnapshots;
	static std::atomic<uint64_t> gPipelineStateOverrideGeneration{1};
	static std::atomic<uint32_t> gPipelineStateOverrideReaderCount{0};

	//games rotate through hundreds of PSOs while recording a frame. A larger direct
	//cache prevents ordinary binds from repeatedly reaching the shared PSO registry.
	static thread_local std::array<PipelineBindingCacheEntry, 256> gPipelineBindingCache;
	static std::vector<ID3D12PipelineState*> gRetiredPipelineStates;
	static std::unordered_set<ID3D12PipelineState*> gRetiredPipelineStateSet;

	static std::atomic<bool> gShaderTargetApplyDirty = true;
	std::atomic<bool> gPipelineStateOverridesDirty = true;
	static size_t gGraphicsShaderTargetApplyCursor = 0;
	static size_t gStreamShaderTargetApplyCursor = 0;
	static size_t gUncapturedShaderTargetApplyCursor = 0;
	static std::deque<size_t> gGraphicsShaderTargetRetryQueue;
	static std::deque<size_t> gStreamShaderTargetRetryQueue;
	static std::deque<size_t> gUncapturedShaderTargetRetryQueue;
	static constexpr size_t gMaximumCapturedReplacementAttemptsPerListPerFrame = 32;
	static constexpr size_t gMaximumUncapturedCandidatesPerFrame = 32;
	static size_t gUncapturedCandidateAttemptCount = 0;
	static size_t gReportedUncapturedCandidateAttemptCount = 0;
	static constexpr uint8_t gMaximumShaderTargetApplyFailureCount = 4;

	PixelShaderSelectionStyle gShaderSelectionStyle = PixelShaderSelectionStyle::BluePixelShader;

	//shared pipeline state and replacement bookkeeping

	void MarkShaderTargetApplyDirty()
	{
		gShaderTargetApplyDirty = true;
		gPipelineStateOverridesDirty = true;
	}

	void QueueShaderTargetApplyWork()
	{
		gShaderTargetApplyDirty = true;
	}

	template <typename PipelineInfo>
	static void ResetShaderTargetRetryState(PipelineInfo& pipeline)
	{
		pipeline.shaderTargetApplyFailureCount = 0;
		pipeline.shaderTargetApplyRetryQueued = false;
	}

	template <typename PipelineInfo>
	static void QueueShaderTargetRetry(PipelineInfo& pipeline, std::deque<size_t>& retryQueue, size_t pipelineIndex, const char* pipelineKind)
	{
		if (pipeline.shaderTargetApplyFailureCount < gMaximumShaderTargetApplyFailureCount)
			++pipeline.shaderTargetApplyFailureCount;

		if (pipeline.shaderTargetApplyFailureCount >= gMaximumShaderTargetApplyFailureCount)
		{
			ShaderInjectorGUI::WriteToRuntimeLogWarning(std::string("HookD3D12->QueueShaderTargetRetry: exhausted rebuild attempts for ") + pipelineKind + " PSO=" + StringHelper::PointerToString(pipeline.pipelineState));
			return;
		}

		if (!pipeline.shaderTargetApplyRetryQueued)
		{
			pipeline.shaderTargetApplyRetryQueued = true;
			retryQueue.push_back(pipelineIndex);
		}

		QueueShaderTargetApplyWork();
	}

	CommandListPipelineState& GetCommandListPipelineState(ID3D12GraphicsCommandList* commandList)
	{
		if (commandList == gCachedCommandList && gCachedCommandListState)
			return *gCachedCommandListState;

		std::lock_guard<std::mutex> lock(gCommandListStateRegistryMutex);
		auto& state = gCommandListStates[commandList];

		if (!state)
			state = std::make_unique<CommandListPipelineState>();

		gCachedCommandList = commandList;
		gCachedCommandListState = state.get();

		return *gCachedCommandListState;
	}

	ScopedPipelineActivity::ScopedPipelineActivity(bool trackActivity)
		: shouldTrackActivity(trackActivity)
	{
		if (!shouldTrackActivity)
			return;

		gActivePipelineActivityCount.fetch_add(1, std::memory_order_acq_rel);
	}

	ScopedPipelineActivity::~ScopedPipelineActivity()
	{
		if (!shouldTrackActivity)
			return;

		gActivePipelineActivityCount.fetch_sub(1, std::memory_order_acq_rel);
	}

	bool IsPipelineCreationIdle()
	{
		return gActivePipelineActivityCount.load(std::memory_order_acquire) == 0;
	}

	OverrideSnapshotReadScope::OverrideSnapshotReadScope()
	{
		gPipelineStateOverrideReaderCount.fetch_add(1, std::memory_order_acq_rel);
	}

	OverrideSnapshotReadScope::~OverrideSnapshotReadScope()
	{
		gPipelineStateOverrideReaderCount.fetch_sub(1, std::memory_order_acq_rel);
	}

	void ResetUncapturedReplacementAttempts()
	{
		//force a new immutable binding snapshot so thread-local known-PSO cache
		//entries cannot bypass this reconsideration pass.
		gPipelineStateOverridesDirty.store(true, std::memory_order_release);

		for (auto& pipeline : gGraphicsPipelines)
			ResetShaderTargetRetryState(pipeline);

		for (auto& pipeline : gPipelineStates)
			ResetShaderTargetRetryState(pipeline);

		gGraphicsShaderTargetRetryQueue.clear();
		gStreamShaderTargetRetryQueue.clear();
		gUncapturedShaderTargetRetryQueue.clear();

		for (size_t pipelineIndex = 0; pipelineIndex < gUncapturedPipelineStates.size(); ++pipelineIndex)
		{
			auto& uncaptured = gUncapturedPipelineStates[pipelineIndex];
			UnregisterKnownPipelineStateLocked(uncaptured.pipelineState);
			uncaptured.attemptedReplacement = false;
			uncaptured.retryReplacementOnRootSignatureChange = false;
			ResetShaderTargetRetryState(uncaptured);

			if (uncaptured.cachedBlobHash &&
				(FindEnabledShaderTargetByCachedBlob(uncaptured.cachedBlobHash) >= 0 ||
				 SupportsCachedBlobContentMatching(uncaptured.cachedBlobSize) ||
				 SupportsCachedBlobMetadataMatching(uncaptured.cachedBlobSize)))
			{
				uncaptured.shaderTargetApplyRetryQueued = true;
				gUncapturedShaderTargetRetryQueue.push_back(pipelineIndex);
			}
		}

		//shader-target refreshes can change every match. restart all cursors so existing
		//captured and uncaptured pipelines are reconsidered incrementally.
		gGraphicsShaderTargetApplyCursor = 0;
		gStreamShaderTargetApplyCursor = 0;
		gUncapturedShaderTargetApplyCursor = 0;
	}

	void BackfillReplacementCachedBlobInfo(ShaderTarget::ShaderTargetDisk& replacement, ID3D12PipelineState* pipelineState)
	{
		if (!replacement.pipelineCachedBlobHash.empty())
			return;

		uint64_t cachedBlobHash = 0;
		SIZE_T cachedBlobSize = 0;
		std::vector<uint8_t> cachedBlob;

		if (GetPipelineCachedBlobInfo(pipelineState, cachedBlobHash, cachedBlobSize, &cachedBlob))
		{
			replacement.pipelineCachedBlobHash = Hash::FormatHash(cachedBlobHash);
			replacement.pipelineCachedBlobLength = std::to_string(cachedBlobSize);
			replacement.pipelineCachedBlobPath = ShaderInjectorIO::JoinPath(replacement.replacementDirectory, "OriginalPipelineCachedBlob" + ShaderInjectorIO::extensionBIN);
		}
	}

	void ClearShaderMarkers()
	{
		bool changed = false;

		for (auto& pipeline : gGraphicsPipelines)
		{
			if (pipeline.pixelShaderDisabled)
			{
				pipeline.pixelShaderDisabled = false;
				changed = true;
			}
		}

		for (auto& pipeline : gPipelineStates)
		{
			if (pipeline.vertexShaderDisabled)
			{
				pipeline.vertexShaderDisabled = false;
				changed = true;
			}

			if (pipeline.pixelShaderDisabled)
			{
				pipeline.pixelShaderDisabled = false;
				changed = true;
			}

			if (pipeline.computeShaderDisabled)
			{
				pipeline.computeShaderDisabled = false;
				changed = true;
			}

			if (pipeline.geometryShaderDisabled)
			{
				pipeline.geometryShaderDisabled = false;
				changed = true;
			}

			if (pipeline.hullShaderDisabled)
			{
				pipeline.hullShaderDisabled = false;
				changed = true;
			}

			if (pipeline.domainShaderDisabled)
			{
				pipeline.domainShaderDisabled = false;
				changed = true;
			}
		}

		if (changed)
			MarkShaderTargetApplyDirty();
	}

	void RetirePipelineState(ID3D12PipelineState*& pipelineState)
	{
		if (!pipelineState)
			return;

		UnregisterKnownPipelineStateLocked(pipelineState);

		//command lists recorded by other game threads may still reference this PSO.
		//keep our owning reference alive for the remaining process lifetime rather than risking
		//an asynchronous device removal after a replacement reload.
		if (gRetiredPipelineStateSet.insert(pipelineState).second)
			gRetiredPipelineStates.push_back(pipelineState);

		pipelineState = nullptr;
	}

	void ReleaseMarkerPSO(ID3D12PipelineState*& pipelineState)
	{
		if (!pipelineState)
			return;

		RetirePipelineState(pipelineState);
	}

	void InvalidateShaderMarkerPSOs()
	{
		std::lock_guard<std::mutex> lock(gPipelineMutex);

		for (auto& pipeline : gGraphicsPipelines)
			ReleaseMarkerPSO(pipeline.pipelineStateWithoutPixelShader);

		for (auto& pipeline : gPipelineStates)
		{
			ReleaseMarkerPSO(pipeline.pipelineStateWithoutVertexShader);
			ReleaseMarkerPSO(pipeline.pipelineStateWithoutPixelShader);
			ReleaseMarkerPSO(pipeline.pipelineStateWithoutComputeShader);
			ReleaseMarkerPSO(pipeline.pipelineStateWithoutGeometryShader);
			ReleaseMarkerPSO(pipeline.pipelineStateWithoutHullShader);
			ReleaseMarkerPSO(pipeline.pipelineStateWithoutDomainShader);
		}

		MarkShaderTargetApplyDirty();
	}

	void ClearReplacementPSO(GraphicsPipelineInfo& pipeline)
	{
		RetirePipelineState(pipeline.pipelineStateWithReplacement);

		pipeline.activeShaderTargetName.clear();
		pipeline.activeShaderTargetType = ShaderTarget::Unknown;
		pipeline.activeShaderTargetHash = 0;
		pipeline.activeShaderTargetUsesFallback = false;

		ResetShaderTargetRetryState(pipeline);
	}

	void ClearReplacementPSO(PipelineStateInfo& pipeline)
	{
		RetirePipelineState(pipeline.pipelineStateWithReplacement);

		pipeline.activeShaderTargetName.clear();
		pipeline.activeShaderTargetType = ShaderTarget::Unknown;
		pipeline.activeShaderTargetHash = 0;
		pipeline.activeShaderTargetUsesFallback = false;

		ResetShaderTargetRetryState(pipeline);
	}

	void InvalidateAllReplacementPSOs()
	{
		gPipelineStateOverridesDirty.store(true, std::memory_order_release);
		std::unordered_set<ID3D12PipelineState*> trackedReplacementPSOs;

		for (const auto& pipeline : gGraphicsPipelines)
		{
			if (pipeline.pipelineStateWithReplacement)
				trackedReplacementPSOs.insert(pipeline.pipelineStateWithReplacement);
		}

		for (const auto& pipeline : gPipelineStates)
		{
			if (pipeline.pipelineStateWithReplacement)
				trackedReplacementPSOs.insert(pipeline.pipelineStateWithReplacement);
		}

		for (auto& pipeline : gGraphicsPipelines)
			ClearReplacementPSO(pipeline);

		for (auto& pipeline : gPipelineStates)
			ClearReplacementPSO(pipeline);

		for (auto& uncaptured : gUncapturedPipelineStates)
		{
			UnregisterKnownPipelineStateLocked(uncaptured.pipelineState);

			if (uncaptured.replacementPipelineState && trackedReplacementPSOs.find(uncaptured.replacementPipelineState) == trackedReplacementPSOs.end())
				RetirePipelineState(uncaptured.replacementPipelineState);
			else
				uncaptured.replacementPipelineState = nullptr;

			uncaptured.activeShaderTargetName.clear();
			uncaptured.activeShaderTargetType = ShaderTarget::Unknown;
			uncaptured.activeShaderTargetHash = 0;
			uncaptured.attemptedReplacement = false;
			uncaptured.retryReplacementOnRootSignatureChange = false;

			ResetShaderTargetRetryState(uncaptured);
		}

		gPipelineStateOverrides.clear();
		gPipelineStateOverridesDirty = true;
		gGraphicsShaderTargetRetryQueue.clear();
		gStreamShaderTargetRetryQueue.clear();
		gUncapturedShaderTargetRetryQueue.clear();
		gGraphicsShaderTargetApplyCursor = 0;
		gStreamShaderTargetApplyCursor = 0;
		gUncapturedShaderTargetApplyCursor = 0;
	}

	const PipelineStateInfo* FindUncapturedRebuildTemplateLocked(ID3D12PipelineState* pipelineState)
	{
		const auto found = gUncapturedPipelineStateIndexByPointer.find(pipelineState);

		if (found == gUncapturedPipelineStateIndexByPointer.end() || found->second >= gUncapturedPipelineStates.size())
			return nullptr;

		return gUncapturedPipelineStates[found->second].rebuildTemplate.get();
	}

	void RebuildPipelineStateOverrideMap()
	{
		gPipelineStateOverrides.clear();
		RenderPassRuntime::BeginShaderTargetBindingUpdate();

		for (auto& pipeline : gGraphicsPipelines)
		{
			if (!pipeline.pipelineState)
				continue;

			if (pipeline.pixelShaderDisabled && pipeline.pipelineStateWithoutPixelShader)
			{
				gPipelineStateOverrides[pipeline.pipelineState] = pipeline.pipelineStateWithoutPixelShader;
				continue;
			}

			if (!pipeline.pipelineStateWithReplacement)
				continue;

			const ShaderTarget::ShaderTargetDisk* activeShaderTarget = FindActiveShaderTarget(
				pipeline.activeShaderTargetName,
				pipeline.activeShaderTargetHash,
				pipeline.activeShaderTargetType);

			if (activeShaderTarget)
			{
				RenderPassRuntime::PipelineOutputState outputState{};

				outputState.renderTargetCount = (std::min)(pipeline.originalDescription.NumRenderTargets, static_cast<UINT>(D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT));

				for (UINT renderTargetIndex = 0; renderTargetIndex < outputState.renderTargetCount; ++renderTargetIndex)
					outputState.renderTargetFormats[renderTargetIndex] = pipeline.originalDescription.RTVFormats[renderTargetIndex];

				outputState.depthStencilFormat = pipeline.originalDescription.DSVFormat;
				outputState.sampleCount = pipeline.originalDescription.SampleDesc.Count;

				if (!outputState.sampleCount)
					outputState.sampleCount = 1;

				outputState.sampleQuality = pipeline.originalDescription.SampleDesc.Quality;

				gPipelineStateOverrides[pipeline.pipelineState] = pipeline.pipelineStateWithReplacement;

				RenderPassRuntime::AddShaderTargetBinding(
					pipeline.pipelineState,
					activeShaderTarget->modifiedShaderId,
					pipeline.activeShaderTargetName,
					pipeline.activeShaderTargetHash,
					pipeline.activeShaderTargetType,
					outputState);
			}
		}

		for (auto& pipeline : gPipelineStates)
		{
			if (!pipeline.pipelineState)
				continue;

			if (pipeline.vertexShaderDisabled && pipeline.pipelineStateWithoutVertexShader)
			{
				gPipelineStateOverrides[pipeline.pipelineState] = pipeline.pipelineStateWithoutVertexShader;
				continue;
			}

			if (pipeline.pixelShaderDisabled && pipeline.pipelineStateWithoutPixelShader)
			{
				gPipelineStateOverrides[pipeline.pipelineState] = pipeline.pipelineStateWithoutPixelShader;
				continue;
			}

			if (pipeline.computeShaderDisabled && pipeline.pipelineStateWithoutComputeShader)
			{
				gPipelineStateOverrides[pipeline.pipelineState] = pipeline.pipelineStateWithoutComputeShader;
				continue;
			}

			if (pipeline.geometryShaderDisabled && pipeline.pipelineStateWithoutGeometryShader)
			{
				gPipelineStateOverrides[pipeline.pipelineState] = pipeline.pipelineStateWithoutGeometryShader;
				continue;
			}

			if (pipeline.hullShaderDisabled && pipeline.pipelineStateWithoutHullShader)
			{
				gPipelineStateOverrides[pipeline.pipelineState] = pipeline.pipelineStateWithoutHullShader;
				continue;
			}

			if (pipeline.domainShaderDisabled && pipeline.pipelineStateWithoutDomainShader)
			{
				gPipelineStateOverrides[pipeline.pipelineState] = pipeline.pipelineStateWithoutDomainShader;
				continue;
			}

			if (!pipeline.pipelineStateWithReplacement)
				continue;

			const ShaderTarget::ShaderTargetDisk* activeShaderTarget = FindActiveShaderTarget(pipeline.activeShaderTargetName, pipeline.activeShaderTargetHash, pipeline.activeShaderTargetType);

			if (activeShaderTarget)
			{
				const RenderPassRuntime::PipelineOutputState outputState = ExtractPipelineOutputState(pipeline);

				gPipelineStateOverrides[pipeline.pipelineState] = pipeline.pipelineStateWithReplacement;

				RenderPassRuntime::AddShaderTargetBinding(pipeline.pipelineState, activeShaderTarget->modifiedShaderId, pipeline.activeShaderTargetName, pipeline.activeShaderTargetHash, pipeline.activeShaderTargetType, outputState);
			}
		}

		for (auto& uncaptured : gUncapturedPipelineStates)
		{
			if (!uncaptured.pipelineState || !uncaptured.replacementPipelineState)
				continue;

			const ShaderTarget::ShaderTargetDisk* activeShaderTarget = FindActiveShaderTarget(uncaptured.activeShaderTargetName, uncaptured.activeShaderTargetHash, uncaptured.activeShaderTargetType);

			if (activeShaderTarget)
			{
				gPipelineStateOverrides[uncaptured.pipelineState] = uncaptured.replacementPipelineState;

				RenderPassRuntime::PipelineOutputState outputState{};

				if (uncaptured.rebuildTemplate)
					outputState = ExtractPipelineOutputState(*uncaptured.rebuildTemplate);

				RenderPassRuntime::AddShaderTargetBinding(uncaptured.pipelineState, activeShaderTarget->modifiedShaderId, uncaptured.activeShaderTargetName, uncaptured.activeShaderTargetHash, uncaptured.activeShaderTargetType, outputState);
			}
		}

		RenderPassRuntime::CommitShaderTargetBindingUpdate();

		auto publishedOverrides = std::make_unique<const PipelineStateOverrideMap>(gPipelineStateOverrides);
		const PipelineStateOverrideMap* publishedOverridePointer = publishedOverrides.get();
		gPublishedPipelineStateOverrides.store(publishedOverridePointer, std::memory_order_release);
		gPipelineStateOverrideGeneration.fetch_add(1, std::memory_order_acq_rel);

		if (gOwnedPublishedPipelineStateOverrides)
		{
			gRetiredPipelineStateOverrideSnapshots.push_back(std::move(gOwnedPublishedPipelineStateOverrides));
		}

		gOwnedPublishedPipelineStateOverrides = std::move(publishedOverrides);

		if (gPipelineStateOverrideReaderCount.load(std::memory_order_acquire) == 0)
			gRetiredPipelineStateOverrideSnapshots.clear();

		gPipelineStateOverridesDirty.store(false, std::memory_order_release);
	}

	bool TryResolvePublishedPipelineState(
		ID3D12PipelineState* requestedPipelineState,
		ID3D12PipelineState*& resolvedPipelineState)
	{
		//a dirty map may be missing a newly built replacement or may still contain
		//an invalidated one. The first bind after a real state change takes the
		//synchronized path and republishes it immediately.
		if (gPipelineStateOverridesDirty.load(std::memory_order_acquire))
			return false;

		const uint64_t publishedGeneration = gPipelineStateOverrideGeneration.load(std::memory_order_acquire);
		const size_t bindingCacheIndex = (reinterpret_cast<uintptr_t>(requestedPipelineState) >> 4) % gPipelineBindingCache.size();

		PipelineBindingCacheEntry& bindingCacheEntry = gPipelineBindingCache[bindingCacheIndex];

		if (bindingCacheEntry.requestedPipelineState == requestedPipelineState && bindingCacheEntry.overrideGeneration == publishedGeneration)
		{
			resolvedPipelineState = bindingCacheEntry.resolvedPipelineState;
			return true;
		}

		OverrideSnapshotReadScope readScope;

		if (gPipelineStateOverridesDirty.load(std::memory_order_acquire))
			return false;

		const PipelineStateOverrideMap* publishedOverrides = gPublishedPipelineStateOverrides.load(std::memory_order_acquire);
		const uint64_t stablePublishedGeneration = gPipelineStateOverrideGeneration.load(std::memory_order_acquire);

		//do not cache an old snapshot under the generation of a newly published
		//one. Otherwise this thread can keep binding the original PSO indefinitely.
		if (stablePublishedGeneration != publishedGeneration ||
			gPipelineStateOverridesDirty.load(std::memory_order_acquire))
			return false;

		const auto overrideIt = publishedOverrides->find(requestedPipelineState);

		if (overrideIt != publishedOverrides->end() && overrideIt->second)
		{
			resolvedPipelineState = overrideIt->second;
			bindingCacheEntry = {requestedPipelineState, resolvedPipelineState, stablePublishedGeneration};
			return true;
		}

		if (!IsKnownPipelineStateLocked(requestedPipelineState))
			return false;

		resolvedPipelineState = requestedPipelineState;
		bindingCacheEntry = {requestedPipelineState, resolvedPipelineState, stablePublishedGeneration};
		return true;
	}

	void GatherPipelineInfo(IDXGISwapChain3* swapChain)
	{
		GatherD3D12PipelineInfo(swapChain, gDevice, gCommandQueue, gPipelineInfo);
	}

	//root-signature tracking for uncaptured pipelines

	void UpdateUncapturedPipelineRootSignatureLocked(ID3D12PipelineState* pipelineState, ID3D12RootSignature* rootSignature, bool computeRootSignature)
	{
		auto uncapturedIndexIt = gUncapturedPipelineStateIndexByPointer.find(pipelineState);

		if (uncapturedIndexIt == gUncapturedPipelineStateIndexByPointer.end() || uncapturedIndexIt->second >= gUncapturedPipelineStates.size())
		{
			return;
		}

		const size_t uncapturedIndex = uncapturedIndexIt->second;
		UncapturedPipelineStateInfo& uncaptured = gUncapturedPipelineStates[uncapturedIndex];

		if (uncaptured.replacementPipelineState)
			return;

		//reset clears the command-list root state, but nullptr is not a newly
		//observed root-signature candidate for rebuilding a persisted PSO.
		if (!rootSignature)
			return;

		//a cached PSO that did not match any shader target cannot become a match
		//merely because its command-list root signature changed. Leaving those
		//attempts settled prevents frequently bound PSOs from starving the
		//incremental uncaptured-PSO apply cursor.
		if (uncaptured.attemptedReplacement && !uncaptured.retryReplacementOnRootSignatureChange)
		{
			return;
		}

		ID3D12RootSignature** observedRootSignature = &uncaptured.observedGraphicsRootSignature;
		if (computeRootSignature)
			observedRootSignature = &uncaptured.observedComputeRootSignature;

		if (*observedRootSignature == rootSignature)
			return;

		const bool retryingFailedReplacement = uncaptured.attemptedReplacement && uncaptured.retryReplacementOnRootSignatureChange;

		if (rootSignature)
			rootSignature->AddRef();

		if (*observedRootSignature)
			(*observedRootSignature)->Release();

		*observedRootSignature = rootSignature;

		if (!retryingFailedReplacement)
			return;

		uncaptured.attemptedReplacement = false;
		uncaptured.retryReplacementOnRootSignatureChange = false;
		uncaptured.shaderTargetApplyFailureCount = 0;

		gUncapturedShaderTargetApplyCursor = (std::min)(gUncapturedShaderTargetApplyCursor, uncapturedIndex);

		QueueShaderTargetApplyWork();

		const char* rootSignatureType = "graphics";

		if (computeRootSignature)
			rootSignatureType = "compute";

		ShaderInjectorGUI::WriteToRuntimeLog(
			std::string("HookD3D12->UpdateUncapturedPipelineRootSignatureLocked: Retrying matched uncaptured PSO after observing a new ") +
			rootSignatureType +
			" root signature: pso = " + StringHelper::PointerToString(pipelineState) +
			" root = " + StringHelper::PointerToString(rootSignature));
	}

	//compute root-signature entry point follows the graphics path

	void RecordUncapturedPipelineStateLocked(ID3D12PipelineState* pipelineState, ID3D12RootSignature* observedGraphicsRootSignature, ID3D12RootSignature* observedComputeRootSignature, const char* reason)
	{
		if (!pipelineState)
			return;

		auto uncapturedIndexIt = gUncapturedPipelineStateIndexByPointer.find(pipelineState);

		if (uncapturedIndexIt != gUncapturedPipelineStateIndexByPointer.end() && uncapturedIndexIt->second < gUncapturedPipelineStates.size())
		{
			const size_t uncapturedIndex = uncapturedIndexIt->second;
			auto& info = gUncapturedPipelineStates[uncapturedIndex];
			UpdateUncapturedPipelineRootSignatureLocked(pipelineState, observedGraphicsRootSignature, false);
			UpdateUncapturedPipelineRootSignatureLocked(pipelineState, observedComputeRootSignature, true);
			return;
		}

		UncapturedPipelineStateInfo info{};
		info.pipelineState = pipelineState;

		//this object is queried after the bind returns, and its address is used as
		//a persistent lookup key. Retain it so a game release cannot reuse that key.
		pipelineState->AddRef();
		info.observedGraphicsRootSignature = observedGraphicsRootSignature;
		info.observedComputeRootSignature = observedComputeRootSignature;

		if (info.observedGraphicsRootSignature)
			info.observedGraphicsRootSignature->AddRef();

		if (info.observedComputeRootSignature)
			info.observedComputeRootSignature->AddRef();

		//the hash is sufficient for the normal persisted lookup. avoid copying and
		//synchronously serializing opaque driver blobs from SetPipelineState; the
		//full bytes are acquired later only when content matching is required.
		GetPipelineCachedBlobInfo(pipelineState, info.cachedBlobHash, info.cachedBlobSize, nullptr);

		const size_t uncapturedIndex = gUncapturedPipelineStates.size();
		gUncapturedPipelineStates.push_back(info);
		gUncapturedPipelineStateIndexByPointer[pipelineState] = uncapturedIndex;
		UncapturedPipelineStateInfo& storedPipeline = gUncapturedPipelineStates[uncapturedIndex];

		if (info.cachedBlobHash)
		{
			//persisted targets can be identified from the cheap cached-blob hash at
			//bind time. Put those candidates ahead of the incremental no-match scan so
			//a warm cache cannot leave a visible shader original for many frames.
			if (gLoadedShaderTargetsOnce &&
				(FindEnabledShaderTargetByCachedBlob(info.cachedBlobHash) >= 0 ||
				 SupportsCachedBlobContentMatching(info.cachedBlobSize) ||
				 SupportsCachedBlobMetadataMatching(info.cachedBlobSize)))
			{
				storedPipeline.shaderTargetApplyRetryQueued = true;
				//FIFO also gives earlier candidates a turn during a sustained PSO burst.
				gUncapturedShaderTargetRetryQueue.push_back(uncapturedIndex);
			}
			QueueShaderTargetApplyWork();
		}
		else
		{
			//there is no persisted identity to match later. mark this PSO as settled
			//so every future bind can take the known-PSO fast path.
			storedPipeline.attemptedReplacement = true;
			RegisterKnownPipelineStateLocked(pipelineState);
		}
	}

	HRESULT CreatePipelineStateInternal(ID3D12Device2* device, const D3D12_PIPELINE_STATE_STREAM_DESC* streamDescription, REFIID interfaceId, void** outputPipelineState)
	{
		return Original_CreatePipelineState(device, streamDescription, interfaceId, outputPipelineState);
	}

	void RebuildStreamPSOWithoutStage(PipelineStateInfo& pipeline, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE targetType, ID3D12PipelineState*& outputPipelineState, ID3D12Device* device)
	{
		if (outputPipelineState || pipeline.streamBlob.empty() || !device)
			return;

		const bool hiddenSelection = gShaderSelectionStyle == PixelShaderSelectionStyle::Hidden;

		if (!hiddenSelection && targetType == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS && Globals::markerPixelShaderBlob.empty() && Globals::nullPixelShaderBlob.empty())
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildStreamPSOWithoutStage: marker and null pixel shader blobs are empty, aborting PS marker rebuild");
			return;
		}

		if (!hiddenSelection && targetType == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS && Globals::markerComputeShaderBlob.empty())
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildStreamPSOWithoutStage: marker compute shader blob is empty, aborting CS marker rebuild");
			return;
		}

		//query Device2 because it owns the stream-based creation entry point
		ID3D12Device2* deviceInterface = nullptr;

		if (FAILED(device->QueryInterface(IID_PPV_ARGS(&deviceInterface))))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildStreamPSOWithoutStage: Failed QueryInterface for Device2");
			return;
		}

		std::vector<uint8_t> patchedBlob = pipeline.streamBlob;

		uint8_t* streamCursor = patchedBlob.data();
		uint8_t* streamEnd = streamCursor + patchedBlob.size();

		bool patchedTarget = false;

		auto originalShaderForType = [&](D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type) -> const std::vector<uint8_t>*
		{
			switch (type)
			{
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
					return &pipeline.vertexShaderBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
					return &pipeline.pixelShaderBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
					return &pipeline.computeShaderBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
					return &pipeline.geometryShaderBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
					return &pipeline.hullShaderBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
					return &pipeline.domainShaderBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
					return &pipeline.amplificationShaderBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS:
					return &pipeline.meshShaderBytecode;
				default:
					return nullptr;
			}
		};

		while (streamCursor < streamEnd)
		{
			if (streamCursor + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) > streamEnd)
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildStreamPSOWithoutStage: stream cursor overran the buffer while reading a type");
				break;
			}

			auto type = *reinterpret_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(streamCursor);
			UINT subobjectTypeIndex = static_cast<UINT>(type);

			if (subobjectTypeIndex >= ARRAYSIZE(subobjectSizes))
			{
				ShaderInjectorGUI::WriteToRuntimeLogError(StringHelper::Format("HookD3D12->RebuildStreamPSOWithoutStage: unknown subobject type=%u at offset=%zu, stopping", subobjectTypeIndex, static_cast<size_t>(streamCursor - patchedBlob.data())));
				break;
			}

			size_t subobjectSize = subobjectSizes[subobjectTypeIndex];

			if (streamCursor + subobjectSize > streamEnd)
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildStreamPSOWithoutStage: Stream walk: subobject overruns buffer");
				break;
			}

			if (const std::vector<uint8_t>* originalBytecode = originalShaderForType(type))
			{
				uint8_t* payloadPointer = streamCursor + sizeof(void*);
				D3D12_SHADER_BYTECODE* shaderBytecode = reinterpret_cast<D3D12_SHADER_BYTECODE*>(payloadPointer);

				if (type != targetType)
				{
					shaderBytecode->pShaderBytecode = DataOrNull(*originalBytecode);
					shaderBytecode->BytecodeLength = originalBytecode->size();
				}
				else if (!hiddenSelection && targetType == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS)
				{
					const std::vector<uint8_t>& markerBlob = SelectPixelMarkerBlob();
					shaderBytecode->pShaderBytecode = DataOrNull(markerBlob);
					shaderBytecode->BytecodeLength = markerBlob.size();
				}
				else if (!hiddenSelection && targetType == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS)
				{
					shaderBytecode->pShaderBytecode = Globals::markerComputeShaderBlob.data();
					shaderBytecode->BytecodeLength = Globals::markerComputeShaderBlob.size();
				}
				else
				{
					shaderBytecode->pShaderBytecode = nullptr;
					shaderBytecode->BytecodeLength = 0;
				}

				if (type == targetType)
					patchedTarget = true;
			}

			//always zero out CachedPSO regardless of target -
			//the cached blob pointer is session-specific and will crash on reuse
			if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO)
			{
				uint8_t* payloadPointer = streamCursor + sizeof(void*);
				D3D12_CACHED_PIPELINE_STATE* cached = reinterpret_cast<D3D12_CACHED_PIPELINE_STATE*>(payloadPointer);
				cached->pCachedBlob = nullptr;
				cached->CachedBlobSizeInBytes = 0;
			}

			if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT)
			{
				uint8_t* payloadPointer = streamCursor + sizeof(void*);
				D3D12_INPUT_LAYOUT_DESC* layout = reinterpret_cast<D3D12_INPUT_LAYOUT_DESC*>(payloadPointer);

				//the pInputElementDescs pointer in the blob points to game memory.
				//we can't fix it up easily here without copying the elements,
				//so null it out - most PSOs don't need it for non-VS stages anyway,
				//but if this is a graphics PSO with VS intact, this will cause issues.
				//for now zero it to stop the crash.
				layout->pInputElementDescs = nullptr;
				layout->NumElements = 0;
			}

			//also null out STREAM_OUTPUT which has the same problem
			if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT)
			{
				uint8_t* payloadPointer = streamCursor + sizeof(void*);
				D3D12_STREAM_OUTPUT_DESC* streamOutput = reinterpret_cast<D3D12_STREAM_OUTPUT_DESC*>(payloadPointer);
				streamOutput->pSODeclaration = nullptr;
				streamOutput->NumEntries = 0;
				streamOutput->pBufferStrides = nullptr;
				streamOutput->NumStrides = 0;
			}

			streamCursor += subobjectSize;
		}

		if (!patchedTarget)
		{
			//the target shader type wasn't found in the stream at all
			//this PSO may not actually contain that stage
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildStreamPSOWithoutStage: Target shader type not found in stream blob, aborting");
			deviceInterface->Release();
			return;
		}

		//second pass: fix up pointer-bearing subobjects
		streamCursor = patchedBlob.data();
		streamEnd = streamCursor + patchedBlob.size();

		while (streamCursor < streamEnd)
		{
			auto type = *reinterpret_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(streamCursor);
			UINT subobjectTypeIndex = static_cast<UINT>(type);

			if (subobjectTypeIndex >= ARRAYSIZE(subobjectSizes))
				break;

			size_t subobjectSize = subobjectSizes[subobjectTypeIndex];

			if (streamCursor + subobjectSize > streamEnd)
				break;

			if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT)
			{
				uint8_t* payloadPointer = streamCursor + sizeof(void*);
				D3D12_INPUT_LAYOUT_DESC* layout = reinterpret_cast<D3D12_INPUT_LAYOUT_DESC*>(payloadPointer);
				layout->pInputElementDescs = DataOrNull(pipeline.inputElements);
				layout->NumElements = static_cast<UINT>(pipeline.inputElements.size());
			}

			if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT)
			{
				uint8_t* payloadPointer = streamCursor + sizeof(void*);
				D3D12_STREAM_OUTPUT_DESC* streamOutput = reinterpret_cast<D3D12_STREAM_OUTPUT_DESC*>(payloadPointer);
				streamOutput->pSODeclaration = DataOrNull(pipeline.streamOutputDeclarations);
				streamOutput->NumEntries = static_cast<UINT>(pipeline.streamOutputDeclarations.size());
				streamOutput->pBufferStrides = DataOrNull(pipeline.streamOutputStrides);
				streamOutput->NumStrides = static_cast<UINT>(pipeline.streamOutputStrides.size());
			}

			streamCursor += subobjectSize;
		}

		D3D12_PIPELINE_STATE_STREAM_DESC patchedDesc{};
		patchedDesc.pPipelineStateSubobjectStream = patchedBlob.data();
		patchedDesc.SizeInBytes = patchedBlob.size();

		HRESULT result = CreatePipelineStateInternal(deviceInterface, &patchedDesc, IID_PPV_ARGS(&outputPipelineState));
		deviceInterface->Release();

		if (FAILED(result))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildStreamPSOWithoutStage: failed result=" + StringHelper::FormatHRESULT(result));
			outputPipelineState = nullptr;
		}
		else
		{
			RegisterKnownPipelineStateLocked(outputPipelineState);
		}
	}

	bool GetReplacementBlobForUse(int replacementIndex, const void*& outBytecode, size_t& outBytecodeSize, bool& outUsedFallback)
	{
		outBytecode = nullptr;
		outBytecodeSize = 0;
		outUsedFallback = false;

		if (replacementIndex < 0 || replacementIndex >= (int)gLoadedShaderTargets.size())
			return false;

		ShaderTarget::ShaderTargetDisk& replacement = gLoadedShaderTargets[replacementIndex];

		if (!IsShaderTargetEffectivelyEnabled(replacement))
			return false;

		if (replacementIndex >= (int)gLoadedShaderTargetBlobs.size())
			gLoadedShaderTargetBlobs.resize(gLoadedShaderTargets.size());

		std::vector<uint8_t>& blob = gLoadedShaderTargetBlobs[replacementIndex];

		if (blob.empty() && !replacement.modifiedShaderBlobPath.empty())
		{
			if (ShaderInjectorIO::FileExists(replacement.modifiedShaderBlobPath))
			{
				if (ShaderInjectorIO::LoadDXILBlobFromDisk(replacement.modifiedShaderBlobPath, blob) && !blob.empty())
					ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->GetReplacementBlobForUse: loaded modified blob for " + replacement.name + " bytes=" + std::to_string(blob.size()));
				else
					ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->GetReplacementBlobForUse: failed to load modified blob for " + replacement.name + " path=" + replacement.modifiedShaderBlobPath);
			}
			else
				ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->GetReplacementBlobForUse: modified blob file missing for " + replacement.name + " path=" + replacement.modifiedShaderBlobPath);
		}

		if (!blob.empty())
		{
			outBytecode = blob.data();
			outBytecodeSize = blob.size();
			return true;
		}

		if (replacement.shaderType == ShaderTarget::PixelShader && !Globals::nullPixelShaderBlob.empty())
		{
			outBytecode = Globals::nullPixelShaderBlob.data();
			outBytecodeSize = Globals::nullPixelShaderBlob.size();
			outUsedFallback = true;
			ShaderInjectorGUI::WriteToRuntimeLogWarning("HookD3D12->GetReplacementBlobForUse: using null pixel shader fallback for " + replacement.name);
			return true;
		}

		return false;
	}

	bool RebuildGraphicsPSOWithReplacement(GraphicsPipelineInfo& pipeline, int replacementIndex, uint64_t shaderHash, ShaderTarget::ShaderType shaderType)
	{
		if (!gDevice || replacementIndex < 0 || replacementIndex >= (int)gLoadedShaderTargets.size())
			return false;

		ShaderTarget::ShaderTargetDisk& replacement = gLoadedShaderTargets[replacementIndex];
		BackfillReplacementCachedBlobInfo(replacement, pipeline.pipelineState);

		const bool compiledBlobAvailable = replacementIndex < (int)gLoadedShaderTargetBlobs.size() && !gLoadedShaderTargetBlobs[replacementIndex].empty();
		const bool compiledBlobOnDisk = !replacement.modifiedShaderBlobPath.empty() && ShaderInjectorIO::FileExists(replacement.modifiedShaderBlobPath);

		if (pipeline.pipelineStateWithReplacement && pipeline.activeShaderTargetName == replacement.name && pipeline.activeShaderTargetHash == shaderHash && pipeline.activeShaderTargetType == shaderType)
		{
			if (!pipeline.activeShaderTargetUsesFallback || (!compiledBlobAvailable && !compiledBlobOnDisk))
				return true;
		}

		const void* replacementBytecode = nullptr;
		size_t replacementBytecodeSize = 0;
		bool usedFallback = false;

		if (!GetReplacementBlobForUse(replacementIndex, replacementBytecode, replacementBytecodeSize, usedFallback))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildGraphicsPSOWithReplacement: replacement blob unavailable for " + replacement.name);
			return false;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = pipeline.originalDescription;
		desc.VS = MakeShaderBytecode(pipeline.vertexShaderBytecode);
		desc.PS = MakeShaderBytecode(pipeline.pixelShaderBytecode);
		desc.GS = MakeShaderBytecode(pipeline.geometryShaderBytecode);
		desc.HS = MakeShaderBytecode(pipeline.hullShaderBytecode);
		desc.DS = MakeShaderBytecode(pipeline.domainShaderBytecode);

		desc.InputLayout.pInputElementDescs = DataOrNull(pipeline.inputElements);
		desc.InputLayout.NumElements = static_cast<UINT>(pipeline.inputElements.size());

		desc.StreamOutput.pSODeclaration = DataOrNull(pipeline.streamOutputDeclarations);
		desc.StreamOutput.NumEntries = static_cast<UINT>(pipeline.streamOutputDeclarations.size());

		desc.StreamOutput.pBufferStrides = DataOrNull(pipeline.streamOutputStrides);
		desc.StreamOutput.NumStrides = static_cast<UINT>(pipeline.streamOutputStrides.size());

		desc.CachedPSO =
		{
			nullptr,
			0
		};

		switch (shaderType)
		{
			case ShaderTarget::VertexShader:
				desc.VS =
				{
					replacementBytecode,
					replacementBytecodeSize
				};

				break;
			case ShaderTarget::PixelShader:
				desc.PS =
				{
					replacementBytecode,
					replacementBytecodeSize
				};

				break;
			case ShaderTarget::GeometryShader:
				desc.GS =
				{
					replacementBytecode,
					replacementBytecodeSize
				};

				break;
			case ShaderTarget::HullShader:
				desc.HS =
				{
					replacementBytecode,
					replacementBytecodeSize
				};

				break;
			case ShaderTarget::DomainShader:
				desc.DS =
				{
					replacementBytecode,
					replacementBytecodeSize
				};

				break;
			default:
				return false;
		}

		ID3D12PipelineState* rebuiltPipelineState = nullptr;
		const ULONGLONG rebuildStartTick = GetTickCount64();

		ShaderInjectorIO::WriteToLogFile(StringHelper::Format(
			"HookD3D12->RebuildGraphicsPSOWithReplacement: begin replacement=%s shaderHash=%s originalPSO=%p replacementBytes=%llu",
			replacement.name.c_str(),
			Hash::FormatHash(shaderHash).c_str(),
			pipeline.pipelineState,
			static_cast<unsigned long long>(replacementBytecodeSize)));

		HRESULT result = Original_CreateGraphicsPipelineState(gDevice, &desc, IID_PPV_ARGS(&rebuiltPipelineState));
		const ULONGLONG rebuildDurationMs = GetTickCount64() - rebuildStartTick;

		if (FAILED(result) || !rebuiltPipelineState)
		{
			if (rebuiltPipelineState)
				rebuiltPipelineState->Release();

			HRESULT removedReason = E_POINTER;

			if (gDevice)
				removedReason = gDevice->GetDeviceRemovedReason();

			ShaderInjectorGUI::WriteToRuntimeLogError(
				"HookD3D12->RebuildGraphicsPSOWithReplacement: failed result = " +
				StringHelper::FormatHRESULT(result) +
				" deviceRemovedReason = " +
				StringHelper::FormatHRESULT(removedReason) +
				" replacement = " + replacement.name);

			return false;
		}

		RetirePipelineState(pipeline.pipelineStateWithReplacement);

		pipeline.pipelineStateWithReplacement = rebuiltPipelineState;
		pipeline.activeShaderTargetName = replacement.name;
		pipeline.activeShaderTargetType = shaderType;
		pipeline.activeShaderTargetHash = shaderHash;
		pipeline.activeShaderTargetUsesFallback = usedFallback;

		RegisterKnownPipelineStateLocked(pipeline.pipelineStateWithReplacement);

		gPipelineStateOverridesDirty = true;

		std::string fallbackDescription;

		if (usedFallback)
			fallbackDescription = " (null shader fallback)";

		ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->RebuildGraphicsPSOWithReplacement: Applied graphics shader replacement: " + replacement.name + fallbackDescription + " durationMs=" + std::to_string(rebuildDurationMs));
		return true;
	}

	bool RebuildStreamPSOWithReplacement(PipelineStateInfo& pipeline, int replacementIndex, uint64_t shaderHash, ShaderTarget::ShaderType shaderType, ID3D12RootSignature* rootSignatureOverride = nullptr)
	{
		if (!gDevice || pipeline.streamBlob.empty() || replacementIndex < 0 || replacementIndex >= (int)gLoadedShaderTargets.size())
			return false;

		ShaderTarget::ShaderTargetDisk& replacement = gLoadedShaderTargets[replacementIndex];
		BackfillReplacementCachedBlobInfo(replacement, pipeline.pipelineState);

		const bool compiledBlobAvailable = replacementIndex < (int)gLoadedShaderTargetBlobs.size() && !gLoadedShaderTargetBlobs[replacementIndex].empty();
		const bool compiledBlobOnDisk = !replacement.modifiedShaderBlobPath.empty() && ShaderInjectorIO::FileExists(replacement.modifiedShaderBlobPath);

		if (pipeline.pipelineStateWithReplacement && pipeline.activeShaderTargetName == replacement.name && pipeline.activeShaderTargetHash == shaderHash && pipeline.activeShaderTargetType == shaderType)
		{
			if (!pipeline.activeShaderTargetUsesFallback || (!compiledBlobAvailable && !compiledBlobOnDisk))
				return true;
		}

		const void* replacementBytecode = nullptr;
		size_t replacementBytecodeSize = 0;
		bool usedFallback = false;

		if (!GetReplacementBlobForUse(replacementIndex, replacementBytecode, replacementBytecodeSize, usedFallback))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildStreamPSOWithReplacement: replacement blob unavailable for " + replacement.name);
			return false;
		}

		const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE targetType = SubobjectTypeForShaderType(shaderType);

		if (targetType == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MAX_VALID)
			return false;

		ID3D12Device2* deviceInterface = nullptr;

		if (FAILED(gDevice->QueryInterface(IID_PPV_ARGS(&deviceInterface))))
			return false;

		std::vector<uint8_t> patchedBlob = pipeline.streamBlob;
		uint8_t* streamCursor = patchedBlob.data();
		uint8_t* streamEnd = streamCursor + patchedBlob.size();
		bool patchedTarget = false;
		bool missingRootSignature = false;
		bool missingViewInstancingState = false;
		auto originalShaderForType = [&](D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type) -> const std::vector<uint8_t>*
		{
			switch (type)
			{
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
					return &pipeline.vertexShaderBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
					return &pipeline.pixelShaderBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
					return &pipeline.computeShaderBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
					return &pipeline.geometryShaderBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
					return &pipeline.hullShaderBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
					return &pipeline.domainShaderBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
					return &pipeline.amplificationShaderBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS:
					return &pipeline.meshShaderBytecode;
				default:
					return nullptr;
			}
		};

		while (streamCursor < streamEnd)
		{
			if (streamCursor + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) > streamEnd)
				break;

			auto type = *reinterpret_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(streamCursor);
			UINT subobjectTypeIndex = static_cast<UINT>(type);

			if (subobjectTypeIndex >= ARRAYSIZE(subobjectSizes))
				break;

			size_t subobjectSize = subobjectSizes[subobjectTypeIndex];

			if (streamCursor + subobjectSize > streamEnd)
				break;

			uint8_t* payloadPointer = streamCursor + sizeof(void*);

			if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE)
			{
				ID3D12RootSignature** rootSignature = reinterpret_cast<ID3D12RootSignature**>(payloadPointer);

				if (rootSignatureOverride)
					*rootSignature = rootSignatureOverride;
				else if (pipeline.rootSignature)
					*rootSignature = pipeline.rootSignature;
				else if (!pipeline.pipelineState)
					missingRootSignature = true;
			}
			else if (const std::vector<uint8_t>* originalBytecode = originalShaderForType(type))
			{
				D3D12_SHADER_BYTECODE* shaderBytecode = reinterpret_cast<D3D12_SHADER_BYTECODE*>(payloadPointer);

				if (type == targetType)
				{
					shaderBytecode->pShaderBytecode = replacementBytecode;
					shaderBytecode->BytecodeLength = replacementBytecodeSize;
					patchedTarget = true;
				}
				else
				{
					shaderBytecode->pShaderBytecode = DataOrNull(*originalBytecode);
					shaderBytecode->BytecodeLength = originalBytecode->size();
				}
			}
			else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO)
			{
				D3D12_CACHED_PIPELINE_STATE* cached = reinterpret_cast<D3D12_CACHED_PIPELINE_STATE*>(payloadPointer);
				cached->pCachedBlob = nullptr;
				cached->CachedBlobSizeInBytes = 0;
			}
			else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT)
			{
				D3D12_INPUT_LAYOUT_DESC* layout = reinterpret_cast<D3D12_INPUT_LAYOUT_DESC*>(payloadPointer);
				layout->pInputElementDescs = DataOrNull(pipeline.inputElements);
				layout->NumElements = static_cast<UINT>(pipeline.inputElements.size());
			}
			else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT)
			{
				D3D12_STREAM_OUTPUT_DESC* streamOutput = reinterpret_cast<D3D12_STREAM_OUTPUT_DESC*>(payloadPointer);
				streamOutput->pSODeclaration = DataOrNull(pipeline.streamOutputDeclarations);
				streamOutput->NumEntries = static_cast<UINT>(pipeline.streamOutputDeclarations.size());
				streamOutput->pBufferStrides = DataOrNull(pipeline.streamOutputStrides);
				streamOutput->NumStrides = static_cast<UINT>(pipeline.streamOutputStrides.size());
			}
			else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING)
			{
				D3D12_VIEW_INSTANCING_DESC* viewInstancing = reinterpret_cast<D3D12_VIEW_INSTANCING_DESC*>(payloadPointer);

				if (!pipeline.hasViewInstancing && viewInstancing->ViewInstanceCount > 0)
				{
					//older persisted templates did not serialize the pointed-to locations.
					//refuse to dereference their process-specific pointer.
					missingViewInstancingState = true;
				}
				else
				{
					viewInstancing->ViewInstanceCount = static_cast<UINT>(pipeline.viewInstanceLocations.size());
					viewInstancing->pViewInstanceLocations = DataOrNull(pipeline.viewInstanceLocations);

					if (pipeline.hasViewInstancing)
						viewInstancing->Flags = pipeline.viewInstancingFlags;
				}
			}

			streamCursor += subobjectSize;
		}

		if (missingRootSignature)
		{
			deviceInterface->Release();
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildStreamPSOWithReplacement: persisted stream template needs an observed root signature for " + replacement.name);
			return false;
		}

		if (missingViewInstancingState)
		{
			deviceInterface->Release();
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildStreamPSOWithReplacement: persisted stream template is missing durable view-instancing locations for " + replacement.name + "; recreate this shader target from a fresh capture");
			return false;
		}

		if (!patchedTarget)
		{
			deviceInterface->Release();
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildStreamPSOWithReplacement: selected stream does not contain target stage " + StringHelper::ShaderTypeToString(shaderType) + " for " + replacement.name);
			return false;
		}

		D3D12_PIPELINE_STATE_STREAM_DESC patchedDesc{};
		patchedDesc.pPipelineStateSubobjectStream = patchedBlob.data();
		patchedDesc.SizeInBytes = patchedBlob.size();

		ID3D12PipelineState* rebuiltPipelineState = nullptr;
		const ULONGLONG rebuildStartTick = GetTickCount64();

		ShaderInjectorIO::WriteToLogFile(StringHelper::Format(
			"HookD3D12->RebuildStreamPSOWithReplacement: begin replacement=%s shaderHash=%s originalPSO=%p streamBytes=%llu replacementBytes=%llu rootOverride=%p",
			replacement.name.c_str(),
			Hash::FormatHash(shaderHash).c_str(),
			pipeline.pipelineState,
			static_cast<unsigned long long>(patchedBlob.size()),
			static_cast<unsigned long long>(replacementBytecodeSize),
			rootSignatureOverride));

		HRESULT result = CreatePipelineStateInternal(deviceInterface, &patchedDesc, IID_PPV_ARGS(&rebuiltPipelineState));
		const ULONGLONG rebuildDurationMs = GetTickCount64() - rebuildStartTick;

		if (FAILED(result) || !rebuiltPipelineState)
		{
			if (rebuiltPipelineState)
				rebuiltPipelineState->Release();

			bool attemptedOriginalValidation = false;
			bool originalValidationSucceeded = false;
			HRESULT originalValidationHr = E_FAIL;
			const std::vector<uint8_t>* originalTargetBytecode = originalShaderForType(targetType);

			if (originalTargetBytecode && !originalTargetBytecode->empty())
			{
				attemptedOriginalValidation = true;
				std::vector<uint8_t> originalValidationBlob = patchedBlob;
				uint8_t* validationPtr = originalValidationBlob.data();
				uint8_t* validationEnd = validationPtr + originalValidationBlob.size();

				while (validationPtr < validationEnd)
				{
					if (validationPtr + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) > validationEnd)
						break;

					auto validationType = *reinterpret_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(validationPtr);
					UINT validationTypeIndex = (UINT)validationType;

					if (validationTypeIndex >= ARRAYSIZE(subobjectSizes))
						break;

					size_t validationSubobjectSize = subobjectSizes[validationTypeIndex];

					if (validationPtr + validationSubobjectSize > validationEnd)
						break;

					if (validationType == targetType)
					{
						D3D12_SHADER_BYTECODE* validationBytecode = reinterpret_cast<D3D12_SHADER_BYTECODE*>(validationPtr + sizeof(void*));
						validationBytecode->pShaderBytecode = originalTargetBytecode->data();
						validationBytecode->BytecodeLength = originalTargetBytecode->size();
						break;
					}

					validationPtr += validationSubobjectSize;
				}

				D3D12_PIPELINE_STATE_STREAM_DESC originalValidationDesc{};
				originalValidationDesc.pPipelineStateSubobjectStream = originalValidationBlob.data();
				originalValidationDesc.SizeInBytes = originalValidationBlob.size();

				ID3D12PipelineState* originalValidationPipelineState = nullptr;
				originalValidationHr = CreatePipelineStateInternal(deviceInterface, &originalValidationDesc, IID_PPV_ARGS(&originalValidationPipelineState));
				originalValidationSucceeded = SUCCEEDED(originalValidationHr) && originalValidationPipelineState;

				if (originalValidationPipelineState)
					originalValidationPipelineState->Release();
			}

			deviceInterface->Release();

			HRESULT removedReason = E_POINTER;

			if (gDevice)
				removedReason = gDevice->GetDeviceRemovedReason();

			SIZE_T originalTargetByteCount = 0;

			if (originalTargetBytecode)
				originalTargetByteCount = originalTargetBytecode->size();

			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildStreamPSOWithReplacement: failed result=" + StringHelper::FormatHRESULT(result) + " deviceRemovedReason=" + StringHelper::FormatHRESULT(removedReason) + " replacement=" + replacement.name + " streamBytes=" + std::to_string(patchedBlob.size()) + " root=" + StringHelper::PointerToString(rootSignatureOverride) + " targetType=" + StringHelper::ShaderTypeToString(shaderType) + " replacementBytes=" + std::to_string(replacementBytecodeSize) + " originalTargetBytes=" + std::to_string(originalTargetByteCount) + " vsBytes=" + std::to_string(pipeline.vertexShaderBytecode.size()) + " psBytes=" + std::to_string(pipeline.pixelShaderBytecode.size()) + " inputElements=" + std::to_string(pipeline.inputElements.size()));

			if (attemptedOriginalValidation)
			{
				if (originalValidationSucceeded)
				{
					ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildStreamPSOWithReplacement: original-bytecode validation succeeded; replacement shader bytecode is incompatible with this PSO/root signature: " + replacement.name);
				}
				else
				{
					ShaderInjectorGUI::WriteToRuntimeLogWarning("HookD3D12->RebuildStreamPSOWithReplacement: original-bytecode validation also failed hr=" + std::to_string((unsigned)originalValidationHr) + "; suspect captured stream/template metadata or root signature for " + replacement.name);
				}
			}

			return false;
		}

		deviceInterface->Release();

		RetirePipelineState(pipeline.pipelineStateWithReplacement);

		pipeline.pipelineStateWithReplacement = rebuiltPipelineState;
		pipeline.activeShaderTargetName = replacement.name;
		pipeline.activeShaderTargetType = shaderType;
		pipeline.activeShaderTargetHash = shaderHash;
		pipeline.activeShaderTargetUsesFallback = usedFallback;

		RegisterKnownPipelineStateLocked(pipeline.pipelineStateWithReplacement);

		if (!rootSignatureOverride)
			PersistAppliedStreamPipelineTemplate(replacement, pipeline, -1, shaderType, shaderHash);

		gPipelineStateOverridesDirty = true;
		ID3D12RootSignature* selectedRootSignature = rootSignatureOverride;

		if (!selectedRootSignature)
			selectedRootSignature = pipeline.rootSignature;

		const char* fallbackDescription = "";

		if (usedFallback)
			fallbackDescription = " (null shader fallback)";

		ShaderInjectorGUI::WriteToRuntimeLog(
			"HookD3D12->RebuildStreamPSOWithReplacement: Applied stream shader replacement: " +
			replacement.name +
			" modifiedShader = " + replacement.modifiedShaderId +
			" originalPSO = " + StringHelper::PointerToString(pipeline.pipelineState) +
			" root = " + StringHelper::PointerToString(selectedRootSignature) +
			" viewInstances = " + std::to_string(pipeline.viewInstanceLocations.size()) +
			fallbackDescription +
			" durationMs = " + std::to_string(rebuildDurationMs));

		return true;
	}

	ShaderTargetApplyResult TryApplyGraphicsReplacement(GraphicsPipelineInfo& pipeline)
	{
		const ShaderCandidate candidates[] =
		{
			{pipeline.vertexShaderHash, ShaderTarget::VertexShader},
			{pipeline.pixelShaderHash, ShaderTarget::PixelShader},
			{pipeline.geometryShaderHash, ShaderTarget::GeometryShader},
			{pipeline.hullShaderHash, ShaderTarget::HullShader},
			{pipeline.domainShaderHash, ShaderTarget::DomainShader},
		};

		for (const ShaderCandidate& candidate : candidates)
		{
			const int replacementIndex = FindEnabledShaderTarget(candidate.shaderHash, candidate.shaderType);

			if (replacementIndex >= 0)
			{
				const bool applied = RebuildGraphicsPSOWithReplacement(pipeline, replacementIndex, candidate.shaderHash, candidate.shaderType);

				if (applied)
					return ShaderTargetApplyResult::Applied;

				return ShaderTargetApplyResult::RetryableFailure;
			}
		}

		return ShaderTargetApplyResult::NoMatch;
	}

	ShaderTargetApplyResult TryApplyStreamReplacement(PipelineStateInfo& pipeline)
	{
		const ShaderCandidate candidates[] =
		{
			{pipeline.vertexShaderHash, ShaderTarget::VertexShader},
			{pipeline.pixelShaderHash, ShaderTarget::PixelShader},
			{pipeline.computeShaderHash, ShaderTarget::ComputeShader},
			{pipeline.geometryShaderHash, ShaderTarget::GeometryShader},
			{pipeline.hullShaderHash, ShaderTarget::HullShader},
			{pipeline.domainShaderHash, ShaderTarget::DomainShader},
		};

		for (const ShaderCandidate& candidate : candidates)
		{
			const int replacementIndex = FindEnabledShaderTarget(candidate.shaderHash, candidate.shaderType);

			if (replacementIndex >= 0)
			{
				const bool applied = RebuildStreamPSOWithReplacement(pipeline, replacementIndex, candidate.shaderHash, candidate.shaderType);

				if (applied)
					return ShaderTargetApplyResult::Applied;

				return ShaderTargetApplyResult::RetryableFailure;
			}
		}

		return ShaderTargetApplyResult::NoMatch;
	}
	bool TryApplyPersistedStreamTemplateToUncaptured(UncapturedPipelineStateInfo& uncaptured, int replacementIndex, uint64_t shaderHash, ShaderTarget::ShaderType shaderType, const char* matchMethod)
	{
		if (replacementIndex < 0 || replacementIndex >= (int)gLoadedShaderTargets.size())
			return false;

		ShaderTarget::ShaderTargetDisk& replacement = gLoadedShaderTargets[replacementIndex];
		ShaderTarget::ShaderTargetDisk templateReplacement{};
		std::string selectedTemplateName;
		SIZE_T selectedTemplateMatchingBytes = 0;

		if (!SelectPersistedPipelineTemplateForUncaptured(replacement, uncaptured, templateReplacement, selectedTemplateName, selectedTemplateMatchingBytes))
			return false;

		if (templateReplacement.pipelineStreamBlobPath.empty())
			return false;

		std::string templateLogSuffix;

		if (!selectedTemplateName.empty())
			templateLogSuffix = " template=" + selectedTemplateName;

		templateLogSuffix += " matchBytes=" + std::to_string(static_cast<size_t>(selectedTemplateMatchingBytes));
		templateLogSuffix += " fixedState=";

		if (templateReplacement.pipelineFixedFunctionStateHash.empty())
			templateLogSuffix += "unknown";
		else
			templateLogSuffix += templateReplacement.pipelineFixedFunctionStateHash;

		PipelineStateInfo persistedPipeline{};

		if (!LoadPersistedStreamTemplateFromReplacement(templateReplacement, persistedPipeline))
			return false;

		ID3D12RootSignature* observedRootSignature = nullptr;
		const bool preferComputeRootSignature = shaderType == ShaderTarget::ComputeShader || (persistedPipeline.isCompute && !persistedPipeline.isGraphics);

		if (preferComputeRootSignature)
		{
			observedRootSignature = uncaptured.observedComputeRootSignature;

			if (!observedRootSignature)
				observedRootSignature = uncaptured.observedGraphicsRootSignature;
		}
		else
		{
			observedRootSignature = uncaptured.observedGraphicsRootSignature;

			if (!observedRootSignature)
				observedRootSignature = uncaptured.observedComputeRootSignature;
		}

		ID3D12RootSignature* persistedRootSignature = GetOrCreatePersistedRootSignature(templateReplacement, gDevice);
		bool attemptedAnyRootSignature = false;

		auto TryRebuildWithRootSignature = [&](ID3D12RootSignature* rootSignatureForRebuild, const char* rootSignatureSource) -> bool
		{
			if (!rootSignatureForRebuild)
				return false;

			attemptedAnyRootSignature = true;
			ShaderInjectorGUI::WriteToRuntimeLog(std::string("HookD3D12->TryApplyPersistedStreamTemplateToUncaptured: Uncaptured persisted stream rebuild using ") + rootSignatureSource + ": " + replacement.name + templateLogSuffix);

			if (!RebuildStreamPSOWithReplacement(persistedPipeline, replacementIndex, shaderHash, shaderType, rootSignatureForRebuild))
				return false;

			uncaptured.replacementPipelineState = persistedPipeline.pipelineStateWithReplacement;
			uncaptured.activeShaderTargetName = replacement.name;
			uncaptured.activeShaderTargetType = shaderType;
			uncaptured.activeShaderTargetHash = shaderHash;
			gPipelineStateOverrides[uncaptured.pipelineState] = persistedPipeline.pipelineStateWithReplacement;
			persistedPipeline.pipelineState = uncaptured.pipelineState;
			persistedPipeline.rootSignature = rootSignatureForRebuild;
			uncaptured.rebuildRootSignature = rootSignatureForRebuild;
			auto ownedTemplate = std::make_shared<PipelineStateInfo>(std::move(persistedPipeline));
			RebindPipelineStateInfoPointerFields(*ownedTemplate);
			uncaptured.rebuildTemplate = std::move(ownedTemplate);

			//content matching resolves an opaque warm-cache PSO to one exact persisted
			//fixed-function variant. Remember that identity so later launches select
			//the same cull/depth state directly, which is essential for inside/outside
			//light-volume pipeline pairs.
			PersistObservedPipelineCacheAlias(replacement, selectedTemplateName, uncaptured.cachedBlobHash);

			ShaderInjectorGUI::WriteToRuntimeLog(std::string("HookD3D12->TryApplyPersistedStreamTemplateToUncaptured: Applied uncaptured PSO replacement from persisted stream template by ") + matchMethod + " using " + rootSignatureSource + ": " + replacement.name + templateLogSuffix);
			return true;
		};

		if (persistedRootSignature)
		{
			if (TryRebuildWithRootSignature(persistedRootSignature, "persisted root signature blob"))
				return true;

			if (observedRootSignature && observedRootSignature != persistedRootSignature)
				ShaderInjectorGUI::WriteToRuntimeLogWarning("HookD3D12->TryApplyPersistedStreamTemplateToUncaptured: Persisted root signature rebuild failed; retrying observed command-list root signature: " + replacement.name + templateLogSuffix);
		}

		if (observedRootSignature && observedRootSignature != persistedRootSignature)
		{
			if (TryRebuildWithRootSignature(observedRootSignature, "observed command-list root signature"))
				return true;
		}

		if (!attemptedAnyRootSignature)
			ShaderInjectorGUI::WriteToRuntimeLogWarning("HookD3D12->TryApplyPersistedStreamTemplateToUncaptured: Uncaptured PSO matched persisted stream template, but no root signature is available: " + replacement.name + templateLogSuffix);

		return false;
	}

	bool TryApplyUncapturedReplacement(UncapturedPipelineStateInfo& uncaptured)
	{
		if (uncaptured.attemptedReplacement || uncaptured.replacementPipelineState)
			return false;

		if (!uncaptured.pipelineState)
			return false;

		if (!uncaptured.cachedBlobHash)
		{
			uncaptured.attemptedReplacement = true;
			return false;
		}

		uncaptured.retryReplacementOnRootSignatureChange = false;

		int replacementIndex = FindEnabledShaderTargetByCachedBlob(uncaptured.cachedBlobHash);
		const char* matchMethod = "cached blob hash";
		std::vector<uint8_t> cachedBlob;
		bool loadedCachedBlob = false;

		//prefer the cached blob's stable content before the metadata fallback. a
		//root-signature/length pair can identify a shader family, but local-light
		//pipelines commonly share that metadata while using opposite cull/depth
		//variants for cameras outside and inside the light volume.
		if (replacementIndex < 0 && SupportsCachedBlobContentMatching(uncaptured.cachedBlobSize))
		{
			uint64_t currentCachedBlobHash = 0;
			SIZE_T currentCachedBlobSize = 0;
			loadedCachedBlob = GetPipelineCachedBlobInfo(uncaptured.pipelineState, currentCachedBlobHash, currentCachedBlobSize, &cachedBlob);

			double matchingRatio = 0.0;
			size_t longestMatchingRun = 0;

			if (loadedCachedBlob &&
				currentCachedBlobHash == uncaptured.cachedBlobHash &&
				currentCachedBlobSize == uncaptured.cachedBlobSize)
			{
				replacementIndex = FindEnabledShaderTargetByCachedBlobContent(cachedBlob, matchingRatio, longestMatchingRun);
			}

			if (replacementIndex >= 0)
			{
				uncaptured.cachedBlob = std::move(cachedBlob);
				matchMethod = "verified cached blob content";
				ShaderInjectorGUI::WriteToRuntimeLog(
					"HookD3D12->TryApplyUncapturedReplacement: Verified persisted cached blob content: replacement=" + gLoadedShaderTargets[replacementIndex].name +
					" cachedHash=" + Hash::FormatHash(uncaptured.cachedBlobHash) +
					" matchingRatio=" + std::to_string(matchingRatio) +
					" longestMatchingRun=" + std::to_string(longestMatchingRun));
			}
		}

		if (replacementIndex < 0)
		{
			uint64_t graphicsRootSignatureHash = 0;
			uint64_t computeRootSignatureHash = 0;
			std::vector<uint8_t> rootSignatureBlob;
			const bool hasGraphicsRootSignature = GetRootSignatureBlob(uncaptured.observedGraphicsRootSignature, rootSignatureBlob, graphicsRootSignatureHash);
			const bool hasComputeRootSignature = GetRootSignatureBlob(uncaptured.observedComputeRootSignature, rootSignatureBlob, computeRootSignatureHash);

			int graphicsMetadataMatch = -1;

			if (hasGraphicsRootSignature)
				graphicsMetadataMatch = FindEnabledShaderTargetByCachedBlobMetadata(uncaptured.cachedBlobSize, graphicsRootSignatureHash, false);

			int computeMetadataMatch = -1;

			if (hasComputeRootSignature)
				computeMetadataMatch = FindEnabledShaderTargetByCachedBlobMetadata(uncaptured.cachedBlobSize, computeRootSignatureHash, true);

			if (graphicsMetadataMatch >= 0 && computeMetadataMatch >= 0 &&
				graphicsMetadataMatch != computeMetadataMatch)
			{
				//command lists can retain both graphics and compute root signatures.
				//do not guess when the two independently identify different targets.
				replacementIndex = -1;
			}
			else
			{
				if (graphicsMetadataMatch >= 0)
					replacementIndex = graphicsMetadataMatch;
				else
					replacementIndex = computeMetadataMatch;
			}

			if (replacementIndex >= 0)
			{
				matchMethod = "cached blob length and root signature";

				ShaderInjectorGUI::WriteToRuntimeLog(
					"HookD3D12->TryApplyUncapturedReplacement: Verified persisted cached blob metadata: replacement=" +
					gLoadedShaderTargets[replacementIndex].name +
					" cachedHash=" + Hash::FormatHash(uncaptured.cachedBlobHash) +
					" cachedBytes=" + std::to_string(uncaptured.cachedBlobSize));
			}
		}

		if (replacementIndex < 0)
		{
			uncaptured.attemptedReplacement = true;

			//cached PSOs are often bound before their command list sets the final
			//graphics/compute root signature. An exact-length persisted candidate can
			//become identifiable when that event arrives, so keep only that targeted
			//event-driven retry alive instead of permanently settling it here.
			uncaptured.retryReplacementOnRootSignatureChange = SupportsCachedBlobMetadataMatching(uncaptured.cachedBlobSize);
			return false;
		}

		ShaderTarget::ShaderTargetDisk& replacement = gLoadedShaderTargets[replacementIndex];
		const uint64_t shaderHash = Hash::ParseHashText(replacement.originalShaderBytecodeHash);
		const bool hasPersistedStreamTemplate =
			!replacement.pipelineStreamBlobPath.empty() ||
			std::any_of(
				replacement.pipelineTemplates.begin(),
				replacement.pipelineTemplates.end(),
				[](const ShaderTarget::ShaderPipelineTemplateDisk& pipelineTemplate)
				{
					return !pipelineTemplate.pipelineStreamBlobPath.empty();
				});

		if (!shaderHash || replacement.shaderType == ShaderTarget::Unknown)
		{
			uncaptured.attemptedReplacement = true;
			return false;
		}

		if ((replacement.sourceList == "Stream" || !replacement.pipelineStreamBlobPath.empty()) &&
			TryApplyPersistedStreamTemplateToUncaptured(uncaptured, replacementIndex, shaderHash, replacement.shaderType, matchMethod))
		{
			return true;
		}

		if (replacement.sourceList == "Graphics" && !replacement.pipelineIndex.empty())
		{
			const int pipelineIndex = atoi(replacement.pipelineIndex.c_str());

			if (pipelineIndex >= 0 && pipelineIndex < (int)gGraphicsPipelines.size())
			{
				GraphicsPipelineInfo& pipeline = gGraphicsPipelines[pipelineIndex];

				if (GraphicsPipelineMatchesReplacementTemplate(pipeline, replacement) && RebuildGraphicsPSOWithReplacement(pipeline, replacementIndex, shaderHash, replacement.shaderType))
				{
					uncaptured.replacementPipelineState = pipeline.pipelineStateWithReplacement;
					uncaptured.activeShaderTargetName = replacement.name;
					uncaptured.activeShaderTargetType = replacement.shaderType;
					uncaptured.activeShaderTargetHash = shaderHash;

					gPipelineStateOverrides[uncaptured.pipelineState] = pipeline.pipelineStateWithReplacement;

					ShaderInjectorGUI::WriteToRuntimeLog(std::string("HookD3D12->TryApplyUncapturedReplacement: Applied uncaptured PSO replacement by ") + matchMethod + ": " + replacement.name);
					return true;
				}
			}
		}

		if (replacement.sourceList == "Stream" && !replacement.pipelineIndex.empty())
		{
			const int pipelineIndex = atoi(replacement.pipelineIndex.c_str());

			if (pipelineIndex >= 0 && pipelineIndex < (int)gPipelineStates.size())
			{
				PipelineStateInfo& pipeline = gPipelineStates[pipelineIndex];

				if (StreamPipelineMatchesReplacementTemplate(pipeline, replacement) && RebuildStreamPSOWithReplacement(pipeline, replacementIndex, shaderHash, replacement.shaderType))
				{
					uncaptured.replacementPipelineState = pipeline.pipelineStateWithReplacement;
					uncaptured.activeShaderTargetName = replacement.name;
					uncaptured.activeShaderTargetType = replacement.shaderType;
					uncaptured.activeShaderTargetHash = shaderHash;

					gPipelineStateOverrides[uncaptured.pipelineState] = pipeline.pipelineStateWithReplacement;

					ShaderInjectorGUI::WriteToRuntimeLog(std::string("HookD3D12->TryApplyUncapturedReplacement: Applied uncaptured PSO replacement by ") + matchMethod + ": " + replacement.name);
					return true;
				}
			}
		}

		if (replacement.sourceList != "Stream")
		{
			for (auto& pipeline : gGraphicsPipelines)
			{
				if (GraphicsPipelineMatchesReplacementTemplate(pipeline, replacement) && RebuildGraphicsPSOWithReplacement(pipeline, replacementIndex, shaderHash, replacement.shaderType))
				{
					uncaptured.replacementPipelineState = pipeline.pipelineStateWithReplacement;
					uncaptured.activeShaderTargetName = replacement.name;
					uncaptured.activeShaderTargetType = replacement.shaderType;
					uncaptured.activeShaderTargetHash = shaderHash;

					gPipelineStateOverrides[uncaptured.pipelineState] = pipeline.pipelineStateWithReplacement;

					ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->TryApplyUncapturedReplacement: Applied uncaptured PSO replacement by matching graphics template: " + replacement.name);
					return true;
				}
			}
		}

		if (replacement.sourceList != "Graphics")
		{
			for (auto& pipeline : gPipelineStates)
			{
				if (StreamPipelineMatchesReplacementTemplate(pipeline, replacement) && RebuildStreamPSOWithReplacement(pipeline, replacementIndex, shaderHash, replacement.shaderType))
				{
					uncaptured.replacementPipelineState = pipeline.pipelineStateWithReplacement;
					uncaptured.activeShaderTargetName = replacement.name;
					uncaptured.activeShaderTargetType = replacement.shaderType;
					uncaptured.activeShaderTargetHash = shaderHash;

					gPipelineStateOverrides[uncaptured.pipelineState] = pipeline.pipelineStateWithReplacement;

					ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->TryApplyUncapturedReplacement: Applied uncaptured PSO replacement by matching stream template: " + replacement.name);
					return true;
				}
			}
		}

		uncaptured.attemptedReplacement = true;
		uncaptured.retryReplacementOnRootSignatureChange = hasPersistedStreamTemplate;

		ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->TryApplyUncapturedReplacement: Uncaptured PSO matched replacement cached blob, but no rebuild template is currently available: " + replacement.name);
		return false;
	}

	void ApplyShaderTargetPSOs()
	{
		if (!IsPipelineCreationIdle())
			return;

		const size_t shaderDiscoveryFrameJobBudget = (std::clamp)(Globals::gShaderDiscoveryFrameJobBudget, 1, 65536);
		ShaderAutomaticDiscovery::ProcessQueuedWork(shaderDiscoveryFrameJobBudget);

		if (!Globals::gShaderInjectorEnabled)
			return;

		if (!gShaderTargetApplyDirty)
			return;

		if (!gLoadedShaderTargetsOnce)
			RefreshLoadedShaderTargets();

		std::lock_guard<std::mutex> lock(gPipelineMutex);

		bool capturedReplacementAttempted = false;

		while (!capturedReplacementAttempted && !gGraphicsShaderTargetRetryQueue.empty())
		{
			const size_t pipelineIndex = gGraphicsShaderTargetRetryQueue.front();
			gGraphicsShaderTargetRetryQueue.pop_front();

			if (pipelineIndex >= gGraphicsPipelines.size())
				continue;

			GraphicsPipelineInfo& pipeline = gGraphicsPipelines[pipelineIndex];
			pipeline.shaderTargetApplyRetryQueued = false;
			const ShaderTargetApplyResult result = TryApplyGraphicsReplacement(pipeline);

			if (result == ShaderTargetApplyResult::NoMatch)
			{
				ResetShaderTargetRetryState(pipeline);
				continue;
			}

			capturedReplacementAttempted = true;

			if (result == ShaderTargetApplyResult::Applied)
				ResetShaderTargetRetryState(pipeline);
			else
				QueueShaderTargetRetry(pipeline, gGraphicsShaderTargetRetryQueue, pipelineIndex, "graphics");
		}

		while (!capturedReplacementAttempted && !gStreamShaderTargetRetryQueue.empty())
		{
			const size_t pipelineIndex = gStreamShaderTargetRetryQueue.front();
			gStreamShaderTargetRetryQueue.pop_front();

			if (pipelineIndex >= gPipelineStates.size())
				continue;

			PipelineStateInfo& pipeline = gPipelineStates[pipelineIndex];
			pipeline.shaderTargetApplyRetryQueued = false;
			const ShaderTargetApplyResult result = TryApplyStreamReplacement(pipeline);

			if (result == ShaderTargetApplyResult::NoMatch)
			{
				ResetShaderTargetRetryState(pipeline);
				continue;
			}

			capturedReplacementAttempted = true;

			if (result == ShaderTargetApplyResult::Applied)
				ResetShaderTargetRetryState(pipeline);
			else
				QueueShaderTargetRetry(pipeline, gStreamShaderTargetRetryQueue, pipelineIndex, "stream");
		}

		size_t graphicsAttemptsThisFrame = 0;

		while (gGraphicsShaderTargetApplyCursor < gGraphicsPipelines.size() &&
			   graphicsAttemptsThisFrame < gMaximumCapturedReplacementAttemptsPerListPerFrame &&
			   !capturedReplacementAttempted)
		{
			const size_t pipelineIndex = gGraphicsShaderTargetApplyCursor;
			GraphicsPipelineInfo& pipeline = gGraphicsPipelines[pipelineIndex];
			const ShaderTargetApplyResult result = TryApplyGraphicsReplacement(pipeline);
			++gGraphicsShaderTargetApplyCursor;
			++graphicsAttemptsThisFrame;
			capturedReplacementAttempted = result != ShaderTargetApplyResult::NoMatch;

			if (result == ShaderTargetApplyResult::Applied)
				ResetShaderTargetRetryState(pipeline);
			else if (result == ShaderTargetApplyResult::RetryableFailure)
				QueueShaderTargetRetry(pipeline, gGraphicsShaderTargetRetryQueue, pipelineIndex, "graphics");
		}

		size_t streamAttemptsThisFrame = 0;

		while (gStreamShaderTargetApplyCursor < gPipelineStates.size() &&
			   streamAttemptsThisFrame < gMaximumCapturedReplacementAttemptsPerListPerFrame &&
			   !capturedReplacementAttempted)
		{
			const size_t pipelineIndex = gStreamShaderTargetApplyCursor;
			PipelineStateInfo& pipeline = gPipelineStates[pipelineIndex];
			const ShaderTargetApplyResult result = TryApplyStreamReplacement(pipeline);
			++gStreamShaderTargetApplyCursor;
			++streamAttemptsThisFrame;
			capturedReplacementAttempted = result != ShaderTargetApplyResult::NoMatch;

			if (result == ShaderTargetApplyResult::Applied)
				ResetShaderTargetRetryState(pipeline);
			else if (result == ShaderTargetApplyResult::RetryableFailure)
				QueueShaderTargetRetry(pipeline, gStreamShaderTargetRetryQueue, pipelineIndex, "stream");
		}

		size_t uncapturedCandidatesThisFrame = 0;

		while (!capturedReplacementAttempted && !gUncapturedShaderTargetRetryQueue.empty() &&
			   uncapturedCandidatesThisFrame < gMaximumUncapturedCandidatesPerFrame)
		{
			const size_t pipelineIndex = gUncapturedShaderTargetRetryQueue.front();
			gUncapturedShaderTargetRetryQueue.pop_front();

			if (pipelineIndex >= gUncapturedPipelineStates.size())
				continue;

			UncapturedPipelineStateInfo& uncaptured = gUncapturedPipelineStates[pipelineIndex];
			uncaptured.shaderTargetApplyRetryQueued = false;

			if (uncaptured.replacementPipelineState)
				continue;

			uncaptured.attemptedReplacement = false;
			uncaptured.retryReplacementOnRootSignatureChange = false;
			const bool applied = TryApplyUncapturedReplacement(uncaptured);
			++uncapturedCandidatesThisFrame;
			++gUncapturedCandidateAttemptCount;

			//a failed identity comparison is not a PSO rebuild. keep looking within
			//the bounded candidate budget, but still rebuild at most one PSO per call.
			capturedReplacementAttempted = applied || uncaptured.retryReplacementOnRootSignatureChange;

			if (applied)
			{
				ResetShaderTargetRetryState(uncaptured);

				gPipelineStateOverridesDirty.store(true, std::memory_order_release);
			}
			else if (uncaptured.retryReplacementOnRootSignatureChange)
			{
				if (!uncaptured.attemptedReplacement)
				{
					QueueShaderTargetRetry(uncaptured, gUncapturedShaderTargetRetryQueue, pipelineIndex, "uncaptured");
				}
			}
			else
			{
				ResetShaderTargetRetryState(uncaptured);

				if (uncaptured.attemptedReplacement)
					RegisterKnownPipelineStateLocked(uncaptured.pipelineState);
			}
		}

		size_t uncapturedInspectionsThisFrame = 0;

		while (!capturedReplacementAttempted &&
			   gUncapturedShaderTargetApplyCursor < gUncapturedPipelineStates.size() &&
			   uncapturedInspectionsThisFrame < 256 &&
			   uncapturedCandidatesThisFrame < gMaximumUncapturedCandidatesPerFrame)
		{
			auto& uncaptured = gUncapturedPipelineStates[gUncapturedShaderTargetApplyCursor];
			++gUncapturedShaderTargetApplyCursor;
			++uncapturedInspectionsThisFrame;

			if (uncaptured.replacementPipelineState || uncaptured.attemptedReplacement ||
				uncaptured.shaderTargetApplyRetryQueued)
				continue;

			if (FindEnabledShaderTargetByCachedBlob(uncaptured.cachedBlobHash) < 0 &&
				!SupportsCachedBlobContentMatching(uncaptured.cachedBlobSize) &&
				!SupportsCachedBlobMetadataMatching(uncaptured.cachedBlobSize))
			{
				uncaptured.attemptedReplacement = true;
				RegisterKnownPipelineStateLocked(uncaptured.pipelineState);
				continue;
			}

			const bool applied = TryApplyUncapturedReplacement(uncaptured);
			capturedReplacementAttempted = applied || uncaptured.retryReplacementOnRootSignatureChange;

			if (uncaptured.replacementPipelineState)
				gPipelineStateOverridesDirty.store(true, std::memory_order_release);

			if (applied)
				ResetShaderTargetRetryState(uncaptured);

			else if (uncaptured.retryReplacementOnRootSignatureChange)
			{
				if (!uncaptured.attemptedReplacement)
				{
					QueueShaderTargetRetry(uncaptured, gUncapturedShaderTargetRetryQueue, gUncapturedShaderTargetApplyCursor - 1, "uncaptured");
				}
			}

			//once an uncaptured PSO either has an override or has conclusively failed
			//to match, it no longer needs discovery/root-signature synchronization on
			//every bind. failed persisted rebuilds stay unresolved so a later root
			//signature can still trigger the targeted retry path.
			if (uncaptured.replacementPipelineState ||
				(uncaptured.attemptedReplacement && !uncaptured.retryReplacementOnRootSignatureChange))
			{
				RegisterKnownPipelineStateLocked(uncaptured.pipelineState);
			}

			++uncapturedCandidatesThisFrame;
			++gUncapturedCandidateAttemptCount;
		}

		if (gPipelineStateOverridesDirty.load(std::memory_order_acquire))
			RebuildPipelineStateOverrideMap();

		gShaderTargetApplyDirty =
			gGraphicsShaderTargetApplyCursor < gGraphicsPipelines.size() ||
			gStreamShaderTargetApplyCursor < gPipelineStates.size() ||
			gUncapturedShaderTargetApplyCursor < gUncapturedPipelineStates.size() ||
			!gGraphicsShaderTargetRetryQueue.empty() ||
			!gStreamShaderTargetRetryQueue.empty() ||
			!gUncapturedShaderTargetRetryQueue.empty();

		if (!gShaderTargetApplyDirty && gUncapturedCandidateAttemptCount != gReportedUncapturedCandidateAttemptCount)
		{
			gReportedUncapturedCandidateAttemptCount = gUncapturedCandidateAttemptCount;

			const size_t appliedCount = static_cast<size_t>(std::count_if(gUncapturedPipelineStates.begin(), gUncapturedPipelineStates.end(), [](const auto& pipeline)
																		  { return pipeline.replacementPipelineState != nullptr; }));

			ShaderInjectorIO::WriteToLogFile(StringHelper::Format("HookD3D12->ApplyShaderTargetPSOs: cached-PSO queue drained; observed=%zu candidatesChecked=%zu rebuilt=%zu pending=0", gUncapturedPipelineStates.size(), gUncapturedCandidateAttemptCount, appliedCount));
		}
	}

	void SetRuntimeReady(bool ready)
	{
		gRuntimeReady.store(ready, std::memory_order_release);
	}

	ID3D12Device* GetCapturedDevice()
	{
		//the injector owns this reference for the lifetime of the active D3D12 hook.
		//callers must treat the returned pointer as borrowed.
		return gDevice;
	}

	static bool ComObjectsAreSame(IUnknown* firstObject, IUnknown* secondObject)
	{
		if (!firstObject || !secondObject)
			return false;

		if (firstObject == secondObject)
			return true;

		IUnknown* firstIdentity = nullptr;
		IUnknown* secondIdentity = nullptr;
		const HRESULT firstResult = firstObject->QueryInterface(IID_PPV_ARGS(&firstIdentity));
		const HRESULT secondResult = secondObject->QueryInterface(IID_PPV_ARGS(&secondIdentity));
		const bool sameObject = SUCCEEDED(firstResult) && SUCCEEDED(secondResult) && firstIdentity == secondIdentity;

		if (firstIdentity)
			firstIdentity->Release();

		if (secondIdentity)
			secondIdentity->Release();

		return sameObject;
	}

	static bool DevicesAreSameObject(ID3D12Device* firstDevice, ID3D12Device* secondDevice)
	{
		return ComObjectsAreSame(firstDevice, secondDevice);
	}

	void RegisterSwapChainCommandQueue(IDXGISwapChain3* swapChain, IUnknown* creationDevice)
	{
		if (!swapChain || !creationDevice)
			return;

		ID3D12CommandQueue* commandQueue = nullptr;
		if (FAILED(creationDevice->QueryInterface(IID_PPV_ARGS(&commandQueue))) ||
			commandQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
		{
			if (commandQueue)
				commandQueue->Release();
			return;
		}

		bool registeredNewBinding = false;
		{
			std::lock_guard<std::mutex> lock(gCommandQueueCaptureMutex);

			for (SwapChainCommandQueueBinding& binding : gSwapChainCommandQueueBindings)
			{
				if (!ComObjectsAreSame(binding.swapChain, swapChain))
					continue;

				if (ComObjectsAreSame(binding.commandQueue, commandQueue))
				{
					commandQueue->Release();
					return;
				}

				binding.commandQueue->Release();
				binding.commandQueue = commandQueue;
				registeredNewBinding = true;
				break;
			}

			if (!registeredNewBinding)
			{
				swapChain->AddRef();
				gSwapChainCommandQueueBindings.push_back({swapChain, commandQueue});
				registeredNewBinding = true;
			}
		}

		if (registeredNewBinding)
		{
			ShaderInjectorIO::WriteToLogFile(StringHelper::Format("HookD3D12->RegisterSwapChainCommandQueue: swapChain=%p exactDirectQueue=%p", swapChain, commandQueue));
		}
	}

	static ID3D12CommandQueue* FindRegisteredSwapChainCommandQueue(IDXGISwapChain3* swapChain)
	{
		if (!swapChain)
			return nullptr;

		std::lock_guard<std::mutex> lock(gCommandQueueCaptureMutex);

		for (const SwapChainCommandQueueBinding& binding : gSwapChainCommandQueueBindings)
		{
			if (!ComObjectsAreSame(binding.swapChain, swapChain))
				continue;

			binding.commandQueue->AddRef();
			return binding.commandQueue;
		}

		return nullptr;
	}

	void RememberDirectCommandQueue(ID3D12CommandQueue* commandQueue)
	{
		if (!commandQueue || commandQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
			return;

		ID3D12Device* queueDevice = nullptr;

		if (FAILED(commandQueue->GetDevice(IID_PPV_ARGS(&queueDevice))))
			return;

		const bool deviceMatches = !gDevice || DevicesAreSameObject(gDevice, queueDevice);
		queueDevice->Release();

		if (!deviceMatches)
			return;

		std::lock_guard<std::mutex> lock(gCommandQueueCaptureMutex);

		if (gMostRecentDirectCommandQueue == commandQueue)
		{
			gMostRecentDirectCommandQueueThreadId = GetCurrentThreadId();
			return;
		}

		commandQueue->AddRef();

		if (gMostRecentDirectCommandQueue)
			gMostRecentDirectCommandQueue->Release();

		gMostRecentDirectCommandQueue = commandQueue;
		gMostRecentDirectCommandQueueThreadId = GetCurrentThreadId();
	}

	bool AdoptMostRecentDirectCommandQueue(IDXGISwapChain3* swapChain)
	{
		if (!swapChain)
			return false;

		ID3D12Device* swapChainDevice = nullptr;

		if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&swapChainDevice))))
			return false;

		if (!gDevice)
		{
			gDevice = swapChainDevice;
			swapChainDevice = nullptr;
		}
		else if (!DevicesAreSameObject(gDevice, swapChainDevice))
		{
			swapChainDevice->Release();
			return false;
		}

		if (swapChainDevice)
			swapChainDevice->Release();

		ID3D12CommandQueue* exactCommandQueue = FindRegisteredSwapChainCommandQueue(swapChain);

		if (exactCommandQueue)
		{
			ID3D12Device* exactQueueDevice = nullptr;
			const HRESULT getDeviceResult = exactCommandQueue->GetDevice(IID_PPV_ARGS(&exactQueueDevice));
			const bool exactQueueMatches = SUCCEEDED(getDeviceResult) && DevicesAreSameObject(gDevice, exactQueueDevice);

			if (exactQueueDevice)
				exactQueueDevice->Release();

			if (!exactQueueMatches)
			{
				exactCommandQueue->Release();
				return false;
			}

			if (gCommandQueue != exactCommandQueue)
			{
				//fence values and allocator ownership already reference the previous
				//queue. changing queues after submission would make that synchronization
				//ambiguous, so disable only the overlay and leave shader hooks active.
				if (gOverlaySubmissionCount != 0)
				{
					ShaderInjectorIO::WriteToLogFileError(StringHelper::Format("HookD3D12->AdoptMostRecentDirectCommandQueue: swap-chain queue changed after overlay submission old=%p new=%p; overlay disabled", gCommandQueue, exactCommandQueue));
					gOverlayRenderingDisabled = true;
					exactCommandQueue->Release();
					return false;
				}

				if (gCommandQueue)
					gCommandQueue->Release();

				gCommandQueue = exactCommandQueue;
				exactCommandQueue = nullptr;
			}

			if (exactCommandQueue)
				exactCommandQueue->Release();

			if (!gLoggedExactCommandQueueCaptured)
			{
				const D3D12_COMMAND_QUEUE_DESC queueDescription = gCommandQueue->GetDesc();

				ShaderInjectorIO::WriteToLogFile(StringHelper::Format(
					"HookD3D12->AdoptMostRecentDirectCommandQueue: adopted exact swap-chain queue=%p priority=%d flags=0x%X nodeMask=%u",
					gCommandQueue,
					queueDescription.Priority,
					static_cast<unsigned int>(queueDescription.Flags),
					queueDescription.NodeMask));

				gLoggedExactCommandQueueCaptured = true;
			}

			return true;
		}

		if (gCommandQueue)
			return true;

		ID3D12CommandQueue* candidateQueue = nullptr;
		{
			std::lock_guard<std::mutex> lock(gCommandQueueCaptureMutex);

			if (gMostRecentDirectCommandQueueThreadId == GetCurrentThreadId())
				candidateQueue = gMostRecentDirectCommandQueue;
			else if (gMostRecentDirectCommandQueue && !gLoggedUnsafeFallbackQueue)
			{
				ShaderInjectorIO::WriteToLogFileWarning(StringHelper::Format(
					"HookD3D12->AdoptMostRecentDirectCommandQueue: no exact swap-chain queue and recent queue belongs to another thread queueThread=%lu presentThread=%lu; overlay waiting",
					static_cast<unsigned long>(gMostRecentDirectCommandQueueThreadId),
					static_cast<unsigned long>(GetCurrentThreadId())));

				gLoggedUnsafeFallbackQueue = true;
			}

			if (candidateQueue)
				candidateQueue->AddRef();
		}

		if (!candidateQueue)
			return false;

		ID3D12Device* candidateDevice = nullptr;
		const HRESULT getDeviceResult = candidateQueue->GetDevice(IID_PPV_ARGS(&candidateDevice));
		const bool candidateMatches = SUCCEEDED(getDeviceResult) && DevicesAreSameObject(gDevice, candidateDevice);

		if (candidateDevice)
			candidateDevice->Release();

		if (!candidateMatches)
		{
			candidateQueue->Release();
			return false;
		}

		gCommandQueue = candidateQueue;

		if (!gLoggedCommandQueueCaptured)
		{
			const D3D12_COMMAND_QUEUE_DESC queueDescription = gCommandQueue->GetDesc();

			ShaderInjectorIO::WriteToLogFile(StringHelper::Format(
				"HookD3D12->AdoptMostRecentDirectCommandQueue: captured pre-Present direct queue=%p priority=%d flags=0x%X nodeMask=%u",
				gCommandQueue,
				queueDescription.Priority,
				static_cast<unsigned int>(queueDescription.Flags),
				queueDescription.NodeMask));

			gLoggedCommandQueueCaptured = true;
		}

		return true;
	}

	void LogOverlayDeviceFailure(const char* operation, HRESULT operationResult)
	{
		HRESULT removedReason = E_POINTER;

		if (gDevice)
			removedReason = gDevice->GetDeviceRemovedReason();

		ShaderInjectorIO::WriteToLogFileError(
			std::string("HookD3D12->") + operation +
			" failed result=" + StringHelper::FormatHRESULT(operationResult) +
			" deviceRemovedReason=" + StringHelper::FormatHRESULT(removedReason));
	}

	void ProcessPendingRebuilds()
	{
		if (gPendingRebuilds.empty())
			return;

		std::lock_guard<std::mutex> lock(gPipelineMutex);

		for (auto& rebuildRequest : gPendingRebuilds)
		{
			if (rebuildRequest.pipelineSource == PipelineSourceList::Graphics)
			{
				if (rebuildRequest.pipelineIndex >= static_cast<int>(gGraphicsPipelines.size()))
					continue;

				auto& graphicsPipeline = gGraphicsPipelines[rebuildRequest.pipelineIndex];

				if (!graphicsPipeline.pipelineStateWithoutPixelShader && gDevice)
				{
					D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = graphicsPipeline.originalDescription;
					desc.VS = MakeShaderBytecode(graphicsPipeline.vertexShaderBytecode);

					const bool hiddenSelection = gShaderSelectionStyle == PixelShaderSelectionStyle::Hidden;
					const std::vector<uint8_t>& markerBlob = SelectPixelMarkerBlob();

					if (hiddenSelection || markerBlob.empty())
						desc.PS = {nullptr, 0};
					else
						desc.PS = {markerBlob.data(), markerBlob.size()};

					desc.InputLayout.pInputElementDescs = DataOrNull(graphicsPipeline.inputElements);
					desc.InputLayout.NumElements = static_cast<UINT>(graphicsPipeline.inputElements.size());
					desc.StreamOutput.pSODeclaration = DataOrNull(graphicsPipeline.streamOutputDeclarations);
					desc.StreamOutput.NumEntries = static_cast<UINT>(graphicsPipeline.streamOutputDeclarations.size());
					desc.StreamOutput.pBufferStrides = DataOrNull(graphicsPipeline.streamOutputStrides);
					desc.StreamOutput.NumStrides = static_cast<UINT>(graphicsPipeline.streamOutputStrides.size());
					desc.CachedPSO = {nullptr, 0};

					HRESULT result = Original_CreateGraphicsPipelineState(gDevice, &desc, IID_PPV_ARGS(&graphicsPipeline.pipelineStateWithoutPixelShader));

					if (SUCCEEDED(result))
						RegisterKnownPipelineStateLocked(graphicsPipeline.pipelineStateWithoutPixelShader);

					if (FAILED(result))
					{
						graphicsPipeline.pixelShaderDisabled = false;

						//dump desc fields to diagnose E_INVALIDARG
						char dbg[1024];
						sprintf_s(dbg,
								  "Rebuilding Graphics PSO:\n"
								  "  pRootSignature:     %p\n"
								  "  VS: ptr=%p size=%zu\n"
								  "  PS: ptr=%p size=%zu\n"
								  "  InputLayout: ptr=%p num=%u\n"
								  "  StreamOutput: soDecl=%p entries=%u strides=%p numStrides=%u\n"
								  "  CachedPSO: ptr=%p size=%zu\n"
								  "  NumRenderTargets:   %u\n"
								  "  RTVFormats[0]:      %u\n"
								  "  DSVFormat:          %u\n"
								  "  SampleDesc: count=%u quality=%u\n"
								  "  PrimitiveTopologyType: %u\n"
								  "  BlendState.RenderTarget[0].BlendEnable: %d\n",
								  desc.pRootSignature,
								  desc.VS.pShaderBytecode, desc.VS.BytecodeLength,
								  desc.PS.pShaderBytecode, desc.PS.BytecodeLength,
								  desc.InputLayout.pInputElementDescs, desc.InputLayout.NumElements,
								  desc.StreamOutput.pSODeclaration, desc.StreamOutput.NumEntries,
								  desc.StreamOutput.pBufferStrides, desc.StreamOutput.NumStrides,
								  desc.CachedPSO.pCachedBlob, desc.CachedPSO.CachedBlobSizeInBytes,
								  desc.NumRenderTargets,
								  desc.RTVFormats[0],
								  desc.DSVFormat,
								  desc.SampleDesc.Count, desc.SampleDesc.Quality,
								  desc.PrimitiveTopologyType,
								  desc.BlendState.RenderTarget[0].BlendEnable);

						MessageBoxA(nullptr, dbg, "PSO Rebuild Desc", MB_OK);
					}
				}
			}
			else if (rebuildRequest.pipelineSource == PipelineSourceList::Stream)
			{
				if (rebuildRequest.pipelineIndex >= static_cast<int>(gPipelineStates.size()))
					continue;

				auto& pipeline = gPipelineStates[rebuildRequest.pipelineIndex];

				ID3D12PipelineState** outputPipelineState = nullptr;

				switch (rebuildRequest.targetSubobjectType)
				{
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
						outputPipelineState = &pipeline.pipelineStateWithoutVertexShader;
						break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
						outputPipelineState = &pipeline.pipelineStateWithoutPixelShader;
						break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
						outputPipelineState = &pipeline.pipelineStateWithoutComputeShader;
						break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
						outputPipelineState = &pipeline.pipelineStateWithoutGeometryShader;
						break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
						outputPipelineState = &pipeline.pipelineStateWithoutHullShader;
						break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
						outputPipelineState = &pipeline.pipelineStateWithoutDomainShader;
						break;
					default:
						continue;
				}

				if (outputPipelineState && !*outputPipelineState)
					RebuildStreamPSOWithoutStage(pipeline, rebuildRequest.targetSubobjectType, *outputPipelineState, gDevice);
			}
		}

		gPendingRebuilds.clear();
	}

	void InstallPipelineHooks()
	{
		InstallPipelineHooksForDevice(gDevice);
	}

	void InstallCommandListHooks()
	{
		InstallCommandListHooksForCommandList(gCommandList);
	}

	static HMODULE FindRTSSHookModule()
	{
#if defined(_WIN64)
		if (HMODULE module = GetModuleHandleW(L"RTSSHooks64.dll"))
			return module;
#endif

		return GetModuleHandleW(L"RTSSHooks.dll");
	}

	static void RestoreRTSSSwapChainCompatibilityLocked()
	{
		if (gRTSSCompatibilitySwapChain && gRTSSCompatibilitySwapChainVTable)
		{
			void** currentVTable = *reinterpret_cast<void***>(gRTSSCompatibilitySwapChain);

			//another overlay may have replaced our table after installation. only
			//restore the pointer when this object still owns the active table.
			if (currentVTable == gRTSSCompatibilitySwapChainVTable)
			{
				InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(gRTSSCompatibilitySwapChain), gRTSSOriginalSwapChainVTable);
			}
		}

		if (gRTSSCompatibilitySwapChain)
			gRTSSCompatibilitySwapChain->Release();

		delete[] gRTSSCompatibilitySwapChainVTable;
		gRTSSCompatibilitySwapChain = nullptr;
		gRTSSOriginalSwapChainVTable = nullptr;
		gRTSSCompatibilitySwapChainVTable = nullptr;
		gRTSSOriginalPresent = nullptr;
		gRTSSOriginalPresent1 = nullptr;
		gRTSSOriginalResizeBuffers = nullptr;
	}

	bool InstallSwapChainCompatibility(IDXGISwapChain3* swapChain, const char* compatibilitySource)
	{
		if (!swapChain)
			return false;

		std::lock_guard<std::mutex> lock(gRTSSCompatibilityMutex);
		void** currentVTable = *reinterpret_cast<void***>(swapChain);

		if (swapChain == gRTSSCompatibilitySwapChain && currentVTable == gRTSSCompatibilitySwapChainVTable)
			return true;

		RestoreRTSSSwapChainCompatibilityLocked();
		currentVTable = *reinterpret_cast<void***>(swapChain);

		void** compatibilityVTable = new void*[gSwapChain3VTableEntryCount];

		std::memcpy(compatibilityVTable, currentVTable, gSwapChain3VTableEntryCount * sizeof(void*));

		FunctionPresentD3D12 currentPresent = reinterpret_cast<FunctionPresentD3D12>(currentVTable[VTableIndex::indexPresent]);
		FunctionPresent1D3D12 currentPresent1 = reinterpret_cast<FunctionPresent1D3D12>(currentVTable[VTableIndex::indexPresent1]);
		FunctionResizeBuffersD3D12 currentResizeBuffers = reinterpret_cast<FunctionResizeBuffersD3D12>(currentVTable[VTableIndex::indexResizeBuffers]);

		compatibilityVTable[VTableIndex::indexPresent] = reinterpret_cast<void*>(&Hook_RTSSCompatibilityPresent);
		compatibilityVTable[VTableIndex::indexPresent1] = reinterpret_cast<void*>(&Hook_RTSSCompatibilityPresent1);
		compatibilityVTable[VTableIndex::indexResizeBuffers] = reinterpret_cast<void*>(&Hook_RTSSCompatibilityResizeBuffers);

		//publish every downstream pointer before making the private table visible
		//to other threads that may already be presenting this swap chain.
		swapChain->AddRef();
		gRTSSCompatibilitySwapChain = swapChain;
		gRTSSOriginalSwapChainVTable = currentVTable;
		gRTSSCompatibilitySwapChainVTable = compatibilityVTable;
		gRTSSOriginalPresent = currentPresent;
		gRTSSOriginalPresent1 = currentPresent1;
		gRTSSOriginalResizeBuffers = currentResizeBuffers;

		InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(swapChain), compatibilityVTable);

		const char* sourceName = compatibilitySource;

		if (!sourceName)
			sourceName = "External";

		ShaderInjectorIO::WriteToLogFile(StringHelper::Format(
			"HookD3D12->InstallSwapChainCompatibility: installed source=%s swapChain=%p Present=%p Present1=%p ResizeBuffers=%p",
			sourceName,
			swapChain,
			reinterpret_cast<void*>(currentPresent),
			reinterpret_cast<void*>(currentPresent1),
			reinterpret_cast<void*>(currentResizeBuffers)));

		return true;
	}

	bool InstallRTSSSwapChainCompatibility(IDXGISwapChain3* swapChain)
	{
		if (!FindRTSSHookModule())
			return false;

		return InstallSwapChainCompatibility(swapChain, "RTSS");
	}

	void Release()
	{
		gRuntimeReady.store(false, std::memory_order_release);
		gShutdown = true;
		ShaderAutomaticDiscovery::Shutdown();
		ResetOverlayStartupGate();

		{
			std::lock_guard<std::mutex> lock(gRTSSCompatibilityMutex);
			RestoreRTSSSwapChainCompatibilityLocked();
		}

		WaitForOverlayGPUIdle();
		ReleaseOverlaySwapChainResources(true);

		if (gOverlayFence)
		{
			gOverlayFence->Release();
			gOverlayFence = nullptr;
		}

		if (gFenceEvent)
		{
			CloseHandle(gFenceEvent);
			gFenceEvent = nullptr;
		}

		if (gCommandQueue)
		{
			gCommandQueue->Release();
			gCommandQueue = nullptr;
		}

		{
			std::lock_guard<std::mutex> lock(gCommandQueueCaptureMutex);

			if (gMostRecentDirectCommandQueue)
			{
				gMostRecentDirectCommandQueue->Release();
				gMostRecentDirectCommandQueue = nullptr;
			}

			gMostRecentDirectCommandQueueThreadId = 0;

			for (SwapChainCommandQueueBinding& binding : gSwapChainCommandQueueBindings)
			{
				if (binding.commandQueue)
					binding.commandQueue->Release();

				if (binding.swapChain)
					binding.swapChain->Release();
			}

			gSwapChainCommandQueueBindings.clear();
		}

		RenderPassExecutor::ReleaseResources();
		ReleaseRootSignatureCache();

		for (UncapturedPipelineStateInfo& uncaptured : gUncapturedPipelineStates)
		{
			if (uncaptured.pipelineState)
			{
				uncaptured.pipelineState->Release();
				uncaptured.pipelineState = nullptr;
			}

			if (uncaptured.observedGraphicsRootSignature)
			{
				uncaptured.observedGraphicsRootSignature->Release();
				uncaptured.observedGraphicsRootSignature = nullptr;
			}

			if (uncaptured.observedComputeRootSignature)
			{
				uncaptured.observedComputeRootSignature->Release();
				uncaptured.observedComputeRootSignature = nullptr;
			}
		}

		gUncapturedPipelineStates.clear();
		gUncapturedPipelineStateIndexByPointer.clear();
		gPipelineStateOverrides.clear();
		gPublishedPipelineStateOverrides.store(&gEmptyPipelineStateOverrides, std::memory_order_release);

		if (gDevice2)
		{
			gDevice2->Release();
			gDevice2 = nullptr;
		}

		if (gDevice)
		{
			gDevice->Release();
			gDevice = nullptr;
		}
	}

	bool IsInitialized()
	{
		return gInitialized;
	}
} //namespace HookD3D12
