//HookD3D12.cpp
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

//custom
#include "Hooks.h"
#include "Globals.h"
#include "dsound_proxy.h"
#include "HookD3D12.h"
#include "ShaderTarget/DatabaseShaderTargets.h"
#include "DatabaseGraphicsPSOs.h"
#include "DatabaseStreamPSOs.h"
#include "HookInput.h"
#include "IO/ShaderInjectorIO.h"
#include "ShaderTarget/ShaderTarget.h"
#include "GUI/ShaderInjectorGUI.h"
#include "Hash.h"
#include "FPSCounter.h"
#include "ShaderAutomaticDiscovery.h"
#include "HookD3D12PipelineUtils.h"
#include "HookD3D12ReplacementLookup.h"
#include "HookD3D12ReplacementTemplates.h"
#include "HookD3D12OverlayStartup.h"
#include "HookD3D12PipelineRegistry.h"
#include "HookD3D12RenderPass.h"
#include "RenderPass/RenderPassRuntime.h"
#include "RenderPass/RenderPassExecutor.h"
#include "Performance/PerformanceMetrics.h"
#include "RenderDoc/RenderDocIntegration.h"
#include "VTableIndex.h"
#include "StringHelper.h"
#include "D3D12/HookD3D12HookHandlers.h"
#include "D3D12/HookD3D12RuntimeState.h"

#if defined _M_X64
typedef uint64_t uintx_t;
#elif defined _M_IX86
typedef uint32_t uintx_t;
#endif

namespace HookD3D12
{
	ID3D12Device*                     gDevice = nullptr;
	ID3D12Device*                     gDevice2 = nullptr;
	ID3D12CommandQueue*               gCommandQueue = nullptr;
	static ID3D12CommandQueue*        gMostRecentDirectCommandQueue = nullptr;
	ID3D12DescriptorHeap*             gHeapRTV = nullptr;
	ID3D12DescriptorHeap*             gHeapSRV = nullptr;
	ID3D12GraphicsCommandList*        gCommandList = nullptr;
	ID3D12Fence*                      gOverlayFence = nullptr;
	HANDLE                            gFenceEvent = nullptr;
	UINT64                            gOverlayFenceValue = 0;
	UINT                              gBufferCount = 0;

	struct SwapChainCommandQueueBinding
	{
		IDXGISwapChain3* swapChain = nullptr;
		ID3D12CommandQueue* commandQueue = nullptr;
	};

	FrameContext* gFrameContexts = nullptr;
	bool          gInitialized = false;
	bool          gShutdown = false;
	ULONGLONG     gOverlayInitializedTick = 0;
	bool          gLoggedStartupMenuDelay = false;
	bool          gOverlayRenderingDisabled = false;
	bool          gOverlayDeviceObjectsCreated = false;
	bool          gLoggedPresentHook = false;
	bool          gLoggedPresent1Hook = false;
	static bool          gLoggedCommandQueueCaptured = false;
	static bool          gLoggedExactCommandQueueCaptured = false;
	static bool          gLoggedUnsafeFallbackQueue = false;
	bool          gLoggedOverlayInitialized = false;
	bool          gLoggedOverlayPipelineActivityDelay = false;
	UINT64        gOverlaySubmissionCount = 0;
	static std::mutex    gCommandQueueCaptureMutex;
	static std::vector<SwapChainCommandQueueBinding> gSwapChainCommandQueueBindings;
	std::atomic<uint32_t> gActivePipelineActivityCount = 0;
	static DWORD gMostRecentDirectCommandQueueThreadId = 0;
	thread_local bool gInsideOverlayResourceCreation = false;

	// RTSS can install its swap-chain interception after our process-wide MinHook
	// detour. In that load order, subsequent Presents can bypass our hook entirely.
	// A private vtable for the game's swap chain lets us wrap RTSS's current targets
	// without removing RTSS from the call chain.
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
	static std::atomic<uint64_t> gPipelineStateOverrideGeneration{ 1 };
	static std::atomic<uint32_t> gPipelineStateOverrideReaderCount{ 0 };

	struct PipelineBindingCacheEntry
	{
		ID3D12PipelineState* requestedPipelineState = nullptr;
		ID3D12PipelineState* resolvedPipelineState = nullptr;
		uint64_t overrideGeneration = 0;
	};

	// Games rotate through hundreds of PSOs while recording a frame. A larger direct
	// cache prevents ordinary binds from repeatedly reaching the shared PSO registry.
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
	static constexpr int gMaximumUncapturedReplacementAttemptsPerFrame = 1;
	static constexpr uint8_t gMaximumShaderTargetApplyFailureCount = 4;

	enum class ShaderTargetApplyResult : uint8_t
	{
		NoMatch,
		Applied,
		RetryableFailure
	};

	PixelShaderSelectionStyle gShaderSelectionStyle = PixelShaderSelectionStyle::BluePixelShader;

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE DEVICE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE DEVICE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE DEVICE |||||||||||||||||||||||||||||||||||||||||||||||||||||

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

	ScopedPipelineActivity::ScopedPipelineActivity(bool trackActivity) : trackingActivity(trackActivity)
	{
		if (!trackingActivity)
			return;

		gActivePipelineActivityCount.fetch_add(1, std::memory_order_acq_rel);
	}

	ScopedPipelineActivity::~ScopedPipelineActivity()
	{
		if (!trackingActivity)
			return;

		gActivePipelineActivityCount.fetch_sub(1, std::memory_order_acq_rel);
	}

	bool IsPipelineCreationIdle()
	{
		return gActivePipelineActivityCount.load(std::memory_order_acquire) == 0;
	}

	void ResetUncapturedReplacementAttempts()
	{
		// Force a new immutable binding snapshot so thread-local known-PSO cache
		// entries cannot bypass this reconsideration pass.
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

			if (uncaptured.cachedBlobHash && FindEnabledShaderTargetByCachedBlob(uncaptured.cachedBlobHash) >= 0)
			{
				uncaptured.shaderTargetApplyRetryQueued = true;
				gUncapturedShaderTargetRetryQueue.push_back(pipelineIndex);
			}
		}

		// Shader-target refreshes can change every match. Restart all cursors so existing
		// captured and uncaptured pipelines are reconsidered incrementally.
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
			if (pipeline.psDisabled)
			{
				pipeline.psDisabled = false;
				changed = true;
			}
		}

		for (auto& pipeline : gPipelineStates)
		{
			if (pipeline.vsDisabled) { pipeline.vsDisabled = false; changed = true; }
			if (pipeline.psDisabled) { pipeline.psDisabled = false; changed = true; }
			if (pipeline.csDisabled) { pipeline.csDisabled = false; changed = true; }
			if (pipeline.gsDisabled) { pipeline.gsDisabled = false; changed = true; }
			if (pipeline.hsDisabled) { pipeline.hsDisabled = false; changed = true; }
			if (pipeline.dsDisabled) { pipeline.dsDisabled = false; changed = true; }
		}

		if (changed)
			MarkShaderTargetApplyDirty();
	}

	void RetirePipelineState(ID3D12PipelineState*& pipelineState)
	{
		if (!pipelineState)
			return;

		UnregisterKnownPipelineStateLocked(pipelineState);

		// Command lists recorded by other game threads may still reference this PSO.
		// Keep our owning reference alive for the remaining process lifetime rather than risking
		// an asynchronous device removal after a replacement reload.
		if (gRetiredPipelineStateSet.insert(pipelineState).second)
			gRetiredPipelineStates.push_back(pipelineState);

		pipelineState = nullptr;
	}

	void ReleaseMarkerPSO(ID3D12PipelineState*& pso)
	{
		if (!pso)
			return;

		RetirePipelineState(pso);
	}

	void InvalidateShaderMarkerPSOs()
	{
		std::lock_guard<std::mutex> lock(gPipelineMutex);

		for (auto& pipeline : gGraphicsPipelines)
			ReleaseMarkerPSO(pipeline.psoWithoutPS);

		for (auto& pipeline : gPipelineStates)
		{
			ReleaseMarkerPSO(pipeline.psoWithoutVS);
			ReleaseMarkerPSO(pipeline.psoWithoutPS);
			ReleaseMarkerPSO(pipeline.psoWithoutCS);
			ReleaseMarkerPSO(pipeline.psoWithoutGS);
			ReleaseMarkerPSO(pipeline.psoWithoutHS);
			ReleaseMarkerPSO(pipeline.psoWithoutDS);
		}

		MarkShaderTargetApplyDirty();
	}

	void ClearReplacementPSO(GraphicsPipelineInfo& pipeline)
	{
		RetirePipelineState(pipeline.psoWithReplacement);

		pipeline.activeShaderTargetName.clear();
		pipeline.activeShaderTargetType = ShaderTarget::Unknown;
		pipeline.activeShaderTargetHash = 0;
		pipeline.activeShaderTargetUsesFallback = false;
		ResetShaderTargetRetryState(pipeline);
	}

	void ClearReplacementPSO(PipelineStateInfo& pipeline)
	{
		RetirePipelineState(pipeline.psoWithReplacement);

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
			if (pipeline.psoWithReplacement)
				trackedReplacementPSOs.insert(pipeline.psoWithReplacement);
		}

		for (const auto& pipeline : gPipelineStates)
		{
			if (pipeline.psoWithReplacement)
				trackedReplacementPSOs.insert(pipeline.psoWithReplacement);
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

	void RebuildPipelineStateOverrideMap()
	{
		gPipelineStateOverrides.clear();
		RenderPassRuntime::BeginShaderTargetBindingUpdate();

		for (auto& pipeline : gGraphicsPipelines)
		{
			if (!pipeline.pipelineState)
				continue;

			if (pipeline.psDisabled && pipeline.psoWithoutPS)
			{
				gPipelineStateOverrides[pipeline.pipelineState] = pipeline.psoWithoutPS;
				continue;
			}

			if (!pipeline.psoWithReplacement)
				continue;

			const ShaderTarget::ShaderTargetDisk* activeShaderTarget = FindActiveShaderTarget(
				pipeline.activeShaderTargetName,
				pipeline.activeShaderTargetHash,
				pipeline.activeShaderTargetType);
			if (activeShaderTarget)
			{
				RenderPassRuntime::PipelineOutputState outputState{};

				outputState.renderTargetCount = (std::min)(pipeline.originalDesc.NumRenderTargets,static_cast<UINT>(D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT));

				for (UINT renderTargetIndex = 0; renderTargetIndex < outputState.renderTargetCount; ++renderTargetIndex)
					outputState.renderTargetFormats[renderTargetIndex] = pipeline.originalDesc.RTVFormats[renderTargetIndex];

				outputState.depthStencilFormat = pipeline.originalDesc.DSVFormat;
				outputState.sampleCount = pipeline.originalDesc.SampleDesc.Count ? pipeline.originalDesc.SampleDesc.Count : 1;
				outputState.sampleQuality = pipeline.originalDesc.SampleDesc.Quality;

				gPipelineStateOverrides[pipeline.pipelineState] = pipeline.psoWithReplacement;

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

			if (pipeline.vsDisabled && pipeline.psoWithoutVS) { gPipelineStateOverrides[pipeline.pipelineState] = pipeline.psoWithoutVS; continue; }
			if (pipeline.psDisabled && pipeline.psoWithoutPS) { gPipelineStateOverrides[pipeline.pipelineState] = pipeline.psoWithoutPS; continue; }
			if (pipeline.csDisabled && pipeline.psoWithoutCS) { gPipelineStateOverrides[pipeline.pipelineState] = pipeline.psoWithoutCS; continue; }
			if (pipeline.gsDisabled && pipeline.psoWithoutGS) { gPipelineStateOverrides[pipeline.pipelineState] = pipeline.psoWithoutGS; continue; }
			if (pipeline.hsDisabled && pipeline.psoWithoutHS) { gPipelineStateOverrides[pipeline.pipelineState] = pipeline.psoWithoutHS; continue; }
			if (pipeline.dsDisabled && pipeline.psoWithoutDS) { gPipelineStateOverrides[pipeline.pipelineState] = pipeline.psoWithoutDS; continue; }

			if (!pipeline.psoWithReplacement)
				continue;

			const ShaderTarget::ShaderTargetDisk* activeShaderTarget = FindActiveShaderTarget(pipeline.activeShaderTargetName, pipeline.activeShaderTargetHash, pipeline.activeShaderTargetType);

			if (activeShaderTarget)
			{
				const RenderPassRuntime::PipelineOutputState outputState = ExtractPipelineOutputState(pipeline);

				gPipelineStateOverrides[pipeline.pipelineState] = pipeline.psoWithReplacement;

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

				RenderPassRuntime::AddShaderTargetBinding(uncaptured.pipelineState, activeShaderTarget->modifiedShaderId, uncaptured.activeShaderTargetName, uncaptured.activeShaderTargetHash, uncaptured.activeShaderTargetType);
			}
		}

		RenderPassRuntime::CommitShaderTargetBindingUpdate();

		auto publishedOverrides = std::make_unique<const PipelineStateOverrideMap>(gPipelineStateOverrides);
		const PipelineStateOverrideMap* publishedOverridePointer = publishedOverrides.get();
		gPublishedPipelineStateOverrides.store(publishedOverridePointer, std::memory_order_release);
		gPipelineStateOverrideGeneration.fetch_add(1, std::memory_order_acq_rel);

		if (gOwnedPublishedPipelineStateOverrides)
		{
			gRetiredPipelineStateOverrideSnapshots.push_back(
				std::move(gOwnedPublishedPipelineStateOverrides));
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
		// A dirty map may be missing a newly built replacement or may still contain
		// an invalidated one. The first bind after a real state change takes the
		// synchronized path and republishes it immediately.
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

		struct OverrideSnapshotReadScope
		{
			OverrideSnapshotReadScope()
			{
				gPipelineStateOverrideReaderCount.fetch_add(1, std::memory_order_acq_rel);
			}
			~OverrideSnapshotReadScope()
			{
				gPipelineStateOverrideReaderCount.fetch_sub(1, std::memory_order_acq_rel);
			}
		} readScope;

		if (gPipelineStateOverridesDirty.load(std::memory_order_acquire))
			return false;

		const PipelineStateOverrideMap* publishedOverrides = gPublishedPipelineStateOverrides.load(std::memory_order_acquire);
		const uint64_t stablePublishedGeneration = gPipelineStateOverrideGeneration.load(std::memory_order_acquire);

		const auto overrideIt = publishedOverrides->find(requestedPipelineState);

		if (overrideIt != publishedOverrides->end() && overrideIt->second)
		{
			resolvedPipelineState = overrideIt->second;
			bindingCacheEntry = { requestedPipelineState, resolvedPipelineState, stablePublishedGeneration };
			return true;
		}

		if (!IsKnownPipelineStateLocked(requestedPipelineState))
			return false;

		resolvedPipelineState = requestedPipelineState;
		bindingCacheEntry = { requestedPipelineState, resolvedPipelineState, stablePublishedGeneration };
		return true;
	}


	void GatherPipelineInfo(IDXGISwapChain3* swapChain)
	{
		GatherD3D12PipelineInfo(swapChain, gDevice, gCommandQueue, gPipelineInfo);
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| HOOK - SET GRAPHICS ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| HOOK - SET GRAPHICS ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| HOOK - SET GRAPHICS ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||

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

		// Reset clears the command-list root state, but nullptr is not a newly
		// observed root-signature candidate for rebuilding a persisted PSO.
		if (!rootSignature)
			return;

		// A cached PSO that did not match any shader target cannot become a match
		// merely because its command-list root signature changed. Leaving those
		// attempts settled prevents frequently bound PSOs from starving the
		// incremental uncaptured-PSO apply cursor.
		if (uncaptured.attemptedReplacement && !uncaptured.retryReplacementOnRootSignatureChange)
		{
			return;
		}

		ID3D12RootSignature*& observedRootSignature = computeRootSignature ? uncaptured.observedComputeRootSignature : uncaptured.observedGraphicsRootSignature;

		if (observedRootSignature == rootSignature)
			return;

		const bool retryingFailedReplacement = uncaptured.attemptedReplacement && uncaptured.retryReplacementOnRootSignatureChange;

		if (rootSignature)
			rootSignature->AddRef();

		if (observedRootSignature)
			observedRootSignature->Release();

		observedRootSignature = rootSignature;

		if (!retryingFailedReplacement)
			return;

		uncaptured.attemptedReplacement = false;
		uncaptured.retryReplacementOnRootSignatureChange = false;
		uncaptured.shaderTargetApplyFailureCount = 0;

		gUncapturedShaderTargetApplyCursor = (std::min)(gUncapturedShaderTargetApplyCursor, uncapturedIndex);

		QueueShaderTargetApplyWork();

		ShaderInjectorGUI::WriteToRuntimeLog(
			std::string("HookD3D12->UpdateUncapturedPipelineRootSignatureLocked: Retrying matched uncaptured PSO after observing a new ") +
			(computeRootSignature ? "compute" : "graphics") +
			" root signature: pso=" + StringHelper::PointerToString(pipelineState) +
			" root=" + StringHelper::PointerToString(rootSignature));
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| HOOK - SET COMPUTE ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| HOOK - SET COMPUTE ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| HOOK - SET COMPUTE ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||

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
		info.observedGraphicsRootSignature = observedGraphicsRootSignature;
		info.observedComputeRootSignature = observedComputeRootSignature;

		if (info.observedGraphicsRootSignature)
			info.observedGraphicsRootSignature->AddRef();

		if (info.observedComputeRootSignature)
			info.observedComputeRootSignature->AddRef();

		// The hash is sufficient for the normal persisted lookup. Avoid copying and
		// synchronously serializing opaque driver blobs from SetPipelineState; the
		// full bytes are acquired later only when content matching is required.
		GetPipelineCachedBlobInfo(pipelineState, info.cachedBlobHash, info.cachedBlobSize, nullptr);

		const size_t uncapturedIndex = gUncapturedPipelineStates.size();
		gUncapturedPipelineStates.push_back(info);
		gUncapturedPipelineStateIndexByPointer[pipelineState] = uncapturedIndex;
		UncapturedPipelineStateInfo& storedPipeline = gUncapturedPipelineStates[uncapturedIndex];

		//char buffer[384];
		//sprintf_s(buffer, "HookD3D12->RecordUncapturedPipelineStateLocked: %s uncaptured PSO=%p cachedHash=%s cachedBytes=%zu", reason ? reason : "Bound", pipelineState, info.cachedBlobHash ? Hash::FormatHash(info.cachedBlobHash).c_str() : "<none>", (size_t)info.cachedBlobSize);
		//ShaderInjectorGUI::WriteToRuntimeLog(buffer);

		if (info.cachedBlobHash)
		{
			// Persisted targets can be identified from the cheap cached-blob hash at
			// bind time. Put those candidates ahead of the incremental no-match scan so
			// a warm cache cannot leave a visible shader original for many frames.
			if (gLoadedShaderTargetsOnce && FindEnabledShaderTargetByCachedBlob(info.cachedBlobHash) >= 0)
			{
				storedPipeline.shaderTargetApplyRetryQueued = true;
				gUncapturedShaderTargetRetryQueue.push_front(uncapturedIndex);
			}
			QueueShaderTargetApplyWork();
		}
		else
		{
			// There is no persisted identity to match later. Mark this PSO as settled
			// so every future bind can take the known-PSO fast path.
			storedPipeline.attemptedReplacement = true;
			RegisterKnownPipelineStateLocked(pipelineState);
		}
	}

	HRESULT CreatePipelineStateInternal(ID3D12Device2* device, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, REFIID riid, void** ppPSO)
	{
		return Original_CreatePipelineState(device, desc, riid, ppPSO);
	}

	void RebuildStreamPSOWithoutStage(PipelineStateInfo& p, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE targetType, ID3D12PipelineState*& outPSO, ID3D12Device* device) // ID3D12Device is queried for ID3D12Device2 below.
	{
		if (outPSO || p.streamBlob.empty() || !device)
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

		//IMPORTANT NOTE: this executes
		//char blobInfo[256];
		//sprintf_s(blobInfo, "streamBlob size=%zu targetType=%u", p.streamBlob.size(), (UINT)targetType);
		//MessageBoxA(nullptr, blobInfo, "Stream PSO: blob info", MB_OK);

		// QI for Device2 which has CreatePipelineState
		ID3D12Device2* device2 = nullptr;

		if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device2))))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildStreamPSOWithoutStage: Failed QueryInterface for Device2");
			return;
		}

		std::vector<uint8_t> patchedBlob = p.streamBlob;

		uint8_t* ptr = patchedBlob.data();
		uint8_t* end = ptr + patchedBlob.size();

		bool patchedTarget = false;
		bool patchedCache = false;
		int  iterations = 0;

		auto originalShaderForType = [&](D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type) -> const std::vector<uint8_t>*
		{
			switch (type)
			{
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS: return &p.vsBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS: return &p.psBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS: return &p.csBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS: return &p.gsBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS: return &p.hsBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS: return &p.dsBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS: return &p.asBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS: return &p.msBytecode;
				default: return nullptr;
			}
		};

		while (ptr < end)
		{
			if (ptr + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) > end)
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildStreamPSOWithoutStage: Stream walk: ptr overran end reading type");
				break;
			}

			auto type = *reinterpret_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(ptr);
			UINT typeIdx = (UINT)type;

			if (typeIdx >= ARRAYSIZE(kSubobjectSizes))
			{
				char unk[256];
				sprintf_s(unk, "HookD3D12->RebuildStreamPSOWithoutStage: Unknown typeIdx=%u at offset=%zu, stopping", typeIdx, (size_t)(ptr - patchedBlob.data()));
				ShaderInjectorGUI::WriteToRuntimeLogError(unk);
				break;
			}

			size_t subobjectSize = kSubobjectSizes[typeIdx];

			if (ptr + subobjectSize > end)
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildStreamPSOWithoutStage: Stream walk: subobject overruns buffer");
				break;
			}

			if (const std::vector<uint8_t>* originalBytecode = originalShaderForType(type))
			{
				uint8_t* payloadPtr = ptr + sizeof(void*);
				D3D12_SHADER_BYTECODE* bc = reinterpret_cast<D3D12_SHADER_BYTECODE*>(payloadPtr);

				//[PASSED] test to see if we can re-inject the original bytecode and still be fine (to ensure that we can infact sucessfully rebuild a PSO)
				//bc->pShaderBytecode = p.psBytecode.data();
				//bc->BytecodeLength = p.psBytecode.size();
				
				//[PASSED] test to see if we can nullify shader bytecode (to ensure that we can infact sucessfully rebuild a PSO)
				//bc->pShaderBytecode = nullptr;
				//bc->BytecodeLength = 0

				//[PASSED] test to see if we completely replace the shader pointer with my own copied memory that we control now
				//std::vector<uint8_t> replacementShader;
				//replacementShader = p.psBytecode;
				//bc->pShaderBytecode = replacementShader.data();
				//bc->BytecodeLength = replacementShader.size();

				//[PASSED] test to see if when we modify a byte within the bytecode, if we can trigger an error (E_INVALIDARG), this proves the game is using our bytecode
				//std::vector<uint8_t> replacementShader;
				//replacementShader = p.psBytecode;
				//replacementShader[replacementShader.size() - 1] = 5; //modify byte
				//bc->pShaderBytecode = replacementShader.data();
				//bc->BytecodeLength = replacementShader.size();
				if (type != targetType)
				{
					bc->pShaderBytecode = originalBytecode->empty() ? nullptr : originalBytecode->data();
					bc->BytecodeLength = originalBytecode->size();
				}
				else if (!hiddenSelection && targetType == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS)
				{
					const std::vector<uint8_t>& markerBlob = !Globals::markerPixelShaderBlob.empty() ? Globals::markerPixelShaderBlob : Globals::nullPixelShaderBlob;
					bc->pShaderBytecode = markerBlob.empty() ? nullptr : markerBlob.data();
					bc->BytecodeLength = markerBlob.size();
				}
				else if (!hiddenSelection && targetType == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS)
				{
					bc->pShaderBytecode = Globals::markerComputeShaderBlob.data();
					bc->BytecodeLength = Globals::markerComputeShaderBlob.size();
				}
				else
				{
					bc->pShaderBytecode = nullptr;
					bc->BytecodeLength = 0;
				}

				if (type == targetType)
					patchedTarget = true;
			}

			// Always zero out CachedPSO regardless of target -
			// the cached blob pointer is session-specific and will crash on reuse
			if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO)
			{
				uint8_t* payloadPtr = ptr + sizeof(void*);
				D3D12_CACHED_PIPELINE_STATE* cached = reinterpret_cast<D3D12_CACHED_PIPELINE_STATE*>(payloadPtr);
				cached->pCachedBlob = nullptr;
				cached->CachedBlobSizeInBytes = 0;
				patchedCache = true;
			}

			if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT)
			{
				uint8_t* payloadPtr = ptr + sizeof(void*);
				D3D12_INPUT_LAYOUT_DESC* layout = reinterpret_cast<D3D12_INPUT_LAYOUT_DESC*>(payloadPtr);

				// The pInputElementDescs pointer in the blob points to game memory.
				// We can't fix it up easily here without copying the elements,
				// so null it out - most PSOs don't need it for non-VS stages anyway,
				// but if this is a graphics PSO with VS intact, this will cause issues.
				// For now zero it to stop the crash.
				layout->pInputElementDescs = nullptr;
				layout->NumElements = 0;
			}

			// Also null out STREAM_OUTPUT which has the same problem
			if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT)
			{
				uint8_t* payloadPtr = ptr + sizeof(void*);
				D3D12_STREAM_OUTPUT_DESC* so = reinterpret_cast<D3D12_STREAM_OUTPUT_DESC*>(payloadPtr);
				so->pSODeclaration = nullptr;
				so->NumEntries = 0;
				so->pBufferStrides = nullptr;
				so->NumStrides = 0;
			}

			ptr += subobjectSize;
			iterations++;
		}

		//IMPORTANT NOTE: this executes
		//char patchResult[256];
		//sprintf_s(patchResult, "[HookD3D12]: RebuildStreamPSOWithoutStage() patchedTarget=%d patchedCache=%d iterations=%d", patchedTarget, patchedCache, iterations);
		//ShaderInjectorGUI::WriteToRuntimeLog(patchResult);

		if (!patchedTarget)
		{
			// The target shader type wasn't found in the stream at all
			// This PSO may not actually contain that stage
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildStreamPSOWithoutStage: Target shader type not found in stream blob, aborting");
			device2->Release();
			return;
		}

		// Second pass: fix up pointer-bearing subobjects
		ptr = patchedBlob.data();
		end = ptr + patchedBlob.size();

		while (ptr < end)
		{
			auto type = *reinterpret_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(ptr);
			UINT typeIdx = (UINT)type;

			if (typeIdx >= ARRAYSIZE(kSubobjectSizes)) 
				break;

			size_t subobjectSize = kSubobjectSizes[typeIdx];

			if (ptr + subobjectSize > end) 
				break;

			if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT)
			{
				uint8_t* payloadPtr = ptr + sizeof(void*);
				D3D12_INPUT_LAYOUT_DESC* layout = reinterpret_cast<D3D12_INPUT_LAYOUT_DESC*>(payloadPtr);
				layout->pInputElementDescs = p.inputElements.empty() ? nullptr : p.inputElements.data();
				layout->NumElements = (UINT)p.inputElements.size();
			}

			if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT)
			{
				uint8_t* payloadPtr = ptr + sizeof(void*);
				D3D12_STREAM_OUTPUT_DESC* so = reinterpret_cast<D3D12_STREAM_OUTPUT_DESC*>(payloadPtr);
				so->pSODeclaration = p.soDeclarations.empty() ? nullptr : p.soDeclarations.data();
				so->NumEntries = (UINT)p.soDeclarations.size();
				so->pBufferStrides = p.soStrides.empty() ? nullptr : p.soStrides.data();
				so->NumStrides = (UINT)p.soStrides.size();
			}

			ptr += subobjectSize;
		}

		D3D12_PIPELINE_STATE_STREAM_DESC patchedDesc{};
		patchedDesc.pPipelineStateSubobjectStream = patchedBlob.data();
		patchedDesc.SizeInBytes = patchedBlob.size();

		//ShaderInjectorGUI::WriteToRuntimeLog("[HookD3D12]: RebuildStreamPSOWithoutStage() Calling CreatePipelineState...");

		//IMPORTANT NOTE: we learned a lesson with CreateGraphicsPipelineState earlier
		//where doing device->oCreateGraphicsPipelineState created a recursive loop rather than just doing oCreateGraphicsPipelineState that led to a fatal app error
		//HRESULT hr = device2->CreatePipelineState(&patchedDesc, IID_PPV_ARGS(&outPSO)); //<----------- THIS IS WHERE WE GET APPLICATION FATAL ERROR
		HRESULT hr = CreatePipelineStateInternal(device2, &patchedDesc, IID_PPV_ARGS(&outPSO));
		device2->Release();

		//char result[128];
		//sprintf_s(result, "[HookD3D12]: RebuildStreamPSOWithoutStage() CreatePipelineState hr=0x%08X", (unsigned)hr);
		//ShaderInjectorGUI::WriteToRuntimeLog(char result);

		if (FAILED(hr))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildStreamPSOWithoutStage: RebuildStreamPSOWithoutStage: failed hr=" + std::to_string((unsigned)hr));
			outPSO = nullptr;
		}
		else
		{
			RegisterKnownPipelineStateLocked(outPSO);
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
		
		if (pipeline.psoWithReplacement && pipeline.activeShaderTargetName == replacement.name && pipeline.activeShaderTargetHash == shaderHash && pipeline.activeShaderTargetType == shaderType)
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

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = pipeline.originalDesc;
		desc.VS = { pipeline.vsBytecode.empty() ? nullptr : pipeline.vsBytecode.data(), pipeline.vsBytecode.size() };
		desc.PS = { pipeline.psBytecode.empty() ? nullptr : pipeline.psBytecode.data(), pipeline.psBytecode.size() };
		desc.GS = { pipeline.gsBytecode.empty() ? nullptr : pipeline.gsBytecode.data(), pipeline.gsBytecode.size() };
		desc.HS = { pipeline.hsBytecode.empty() ? nullptr : pipeline.hsBytecode.data(), pipeline.hsBytecode.size() };
		desc.DS = { pipeline.dsBytecode.empty() ? nullptr : pipeline.dsBytecode.data(), pipeline.dsBytecode.size() };
		desc.InputLayout.pInputElementDescs = pipeline.inputElements.empty() ? nullptr : pipeline.inputElements.data();
		desc.InputLayout.NumElements = (UINT)pipeline.inputElements.size();
		desc.StreamOutput.pSODeclaration = pipeline.soDeclarations.empty() ? nullptr : pipeline.soDeclarations.data();
		desc.StreamOutput.NumEntries = (UINT)pipeline.soDeclarations.size();
		desc.StreamOutput.pBufferStrides = pipeline.soStrides.empty() ? nullptr : pipeline.soStrides.data();
		desc.StreamOutput.NumStrides = (UINT)pipeline.soStrides.size();
		desc.CachedPSO = { nullptr, 0 };

		switch (shaderType)
		{
			case ShaderTarget::VertexShader: desc.VS = { replacementBytecode, replacementBytecodeSize }; break;
			case ShaderTarget::PixelShader: desc.PS = { replacementBytecode, replacementBytecodeSize }; break;
			case ShaderTarget::GeometryShader: desc.GS = { replacementBytecode, replacementBytecodeSize }; break;
			case ShaderTarget::HullShader: desc.HS = { replacementBytecode, replacementBytecodeSize }; break;
			case ShaderTarget::DomainShader: desc.DS = { replacementBytecode, replacementBytecodeSize }; break;
			default: return false;
		}

		ID3D12PipelineState* rebuiltPipelineState = nullptr;
		const ULONGLONG rebuildStartTick = GetTickCount64();
		ShaderInjectorIO::WriteToLogFile(StringHelper::Format(
			"HookD3D12->RebuildGraphicsPSOWithReplacement: begin replacement=%s shaderHash=%s originalPSO=%p replacementBytes=%llu",
			replacement.name.c_str(),
			Hash::FormatHash(shaderHash).c_str(),
			pipeline.pipelineState,
			static_cast<unsigned long long>(replacementBytecodeSize)));
		HRESULT hr = Original_CreateGraphicsPipelineState(gDevice, &desc, IID_PPV_ARGS(&rebuiltPipelineState));
		const ULONGLONG rebuildDurationMs = GetTickCount64() - rebuildStartTick;

		if (FAILED(hr) || !rebuiltPipelineState)
		{
			if (rebuiltPipelineState)
				rebuiltPipelineState->Release();

			const HRESULT removedReason =
				gDevice ? gDevice->GetDeviceRemovedReason() : E_POINTER;
			ShaderInjectorGUI::WriteToRuntimeLogError(
				"HookD3D12->RebuildGraphicsPSOWithReplacement: failed hr=" +
				StringHelper::FormatHRESULT(hr) +
				" deviceRemovedReason=" +
				StringHelper::FormatHRESULT(removedReason) +
				" replacement=" + replacement.name);
			return false;
		}

		RetirePipelineState(pipeline.psoWithReplacement);
		pipeline.psoWithReplacement = rebuiltPipelineState;
		pipeline.activeShaderTargetName = replacement.name;
		pipeline.activeShaderTargetType = shaderType;
		pipeline.activeShaderTargetHash = shaderHash;
		pipeline.activeShaderTargetUsesFallback = usedFallback;
		RegisterKnownPipelineStateLocked(pipeline.psoWithReplacement);
		gPipelineStateOverridesDirty = true;
		ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->RebuildGraphicsPSOWithReplacement: Applied graphics shader replacement: " + replacement.name + (usedFallback ? " (null shader fallback)" : "") + " durationMs=" + std::to_string(rebuildDurationMs));
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
		
		if (pipeline.psoWithReplacement && pipeline.activeShaderTargetName == replacement.name && pipeline.activeShaderTargetHash == shaderHash && pipeline.activeShaderTargetType == shaderType)
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

		ID3D12Device2* device2 = nullptr;
		
		if (FAILED(gDevice->QueryInterface(IID_PPV_ARGS(&device2))))
			return false;

		std::vector<uint8_t> patchedBlob = pipeline.streamBlob;
		uint8_t* ptr = patchedBlob.data();
		uint8_t* end = ptr + patchedBlob.size();
		bool patchedTarget = false;
		bool missingRootSignature = false;
		bool missingViewInstancingState = false;
		auto originalShaderForType = [&](D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type) -> const std::vector<uint8_t>*
		{
			switch (type)
			{
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS: return &pipeline.vsBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS: return &pipeline.psBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS: return &pipeline.csBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS: return &pipeline.gsBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS: return &pipeline.hsBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS: return &pipeline.dsBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS: return &pipeline.asBytecode;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS: return &pipeline.msBytecode;
				default: return nullptr;
			}
		};

		while (ptr < end)
		{
			if (ptr + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) > end)
				break;

			auto type = *reinterpret_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(ptr);
			UINT typeIdx = (UINT)type;

			if (typeIdx >= ARRAYSIZE(kSubobjectSizes))
				break;

			size_t subobjectSize = kSubobjectSizes[typeIdx];
			if (ptr + subobjectSize > end)
				break;

			uint8_t* payloadPtr = ptr + sizeof(void*);

			if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE)
			{
				ID3D12RootSignature** rootSignature = reinterpret_cast<ID3D12RootSignature**>(payloadPtr);

				if (rootSignatureOverride)
					*rootSignature = rootSignatureOverride;
				else if (pipeline.rootSignature)
					*rootSignature = pipeline.rootSignature;
				else if (!pipeline.pipelineState)
					missingRootSignature = true;
			}
			else if (const std::vector<uint8_t>* originalBytecode = originalShaderForType(type))
			{
				D3D12_SHADER_BYTECODE* bc = reinterpret_cast<D3D12_SHADER_BYTECODE*>(payloadPtr);

				if (type == targetType)
				{
					bc->pShaderBytecode = replacementBytecode;
					bc->BytecodeLength = replacementBytecodeSize;
					patchedTarget = true;
				}
				else
				{
					bc->pShaderBytecode = originalBytecode->empty() ? nullptr : originalBytecode->data();
					bc->BytecodeLength = originalBytecode->size();
				}
			}
			else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO)
			{
				D3D12_CACHED_PIPELINE_STATE* cached = reinterpret_cast<D3D12_CACHED_PIPELINE_STATE*>(payloadPtr);
				cached->pCachedBlob = nullptr;
				cached->CachedBlobSizeInBytes = 0;
			}
			else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT)
			{
				D3D12_INPUT_LAYOUT_DESC* layout = reinterpret_cast<D3D12_INPUT_LAYOUT_DESC*>(payloadPtr);
				layout->pInputElementDescs = pipeline.inputElements.empty() ? nullptr : pipeline.inputElements.data();
				layout->NumElements = (UINT)pipeline.inputElements.size();
			}
			else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT)
			{
				D3D12_STREAM_OUTPUT_DESC* so = reinterpret_cast<D3D12_STREAM_OUTPUT_DESC*>(payloadPtr);
				so->pSODeclaration = pipeline.soDeclarations.empty() ? nullptr : pipeline.soDeclarations.data();
				so->NumEntries = (UINT)pipeline.soDeclarations.size();
				so->pBufferStrides = pipeline.soStrides.empty() ? nullptr : pipeline.soStrides.data();
				so->NumStrides = (UINT)pipeline.soStrides.size();
			}
			else if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING)
			{
				D3D12_VIEW_INSTANCING_DESC* viewInstancing = reinterpret_cast<D3D12_VIEW_INSTANCING_DESC*>(payloadPtr);

				if (!pipeline.hasViewInstancing && viewInstancing->ViewInstanceCount > 0)
				{
					// Older persisted templates did not serialize the pointed-to locations.
					// Refuse to dereference their process-specific pointer.
					missingViewInstancingState = true;
				}
				else
				{
					viewInstancing->ViewInstanceCount = static_cast<UINT>(pipeline.viewInstanceLocations.size());
					viewInstancing->pViewInstanceLocations =
						pipeline.viewInstanceLocations.empty() ? nullptr : pipeline.viewInstanceLocations.data();
					if (pipeline.hasViewInstancing)
						viewInstancing->Flags = pipeline.viewInstancingFlags;
				}
			}

			ptr += subobjectSize;
		}

		if (missingRootSignature)
		{
			device2->Release();
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildStreamPSOWithReplacement: persisted stream template needs an observed root signature for " + replacement.name);
			return false;
		}

		if (missingViewInstancingState)
		{
			device2->Release();
			ShaderInjectorGUI::WriteToRuntimeLogError(
				"HookD3D12->RebuildStreamPSOWithReplacement: persisted stream template is missing durable view-instancing locations for " +
				replacement.name + "; recreate this shader target from a fresh capture");
			return false;
		}

		if (!patchedTarget)
		{
			device2->Release();
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
		HRESULT hr = CreatePipelineStateInternal(device2, &patchedDesc, IID_PPV_ARGS(&rebuiltPipelineState));
		const ULONGLONG rebuildDurationMs = GetTickCount64() - rebuildStartTick;

		if (FAILED(hr) || !rebuiltPipelineState)
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
					if (validationTypeIndex >= ARRAYSIZE(kSubobjectSizes))
						break;

					size_t validationSubobjectSize = kSubobjectSizes[validationTypeIndex];
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
				originalValidationHr = CreatePipelineStateInternal(device2, &originalValidationDesc, IID_PPV_ARGS(&originalValidationPipelineState));
				originalValidationSucceeded = SUCCEEDED(originalValidationHr) && originalValidationPipelineState;
				if (originalValidationPipelineState)
					originalValidationPipelineState->Release();
			}

			device2->Release();

			const HRESULT removedReason =
				gDevice ? gDevice->GetDeviceRemovedReason() : E_POINTER;
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildStreamPSOWithReplacement: failed hr=" + StringHelper::FormatHRESULT(hr) + " deviceRemovedReason=" + StringHelper::FormatHRESULT(removedReason) + " replacement=" + replacement.name + " streamBytes=" + std::to_string(patchedBlob.size()) + " root=" + StringHelper::PointerToString(rootSignatureOverride) + " targetType=" + StringHelper::ShaderTypeToString(shaderType) + " replacementBytes=" + std::to_string(replacementBytecodeSize) + " originalTargetBytes=" + std::to_string(originalTargetBytecode ? originalTargetBytecode->size() : 0) + " vsBytes=" + std::to_string(pipeline.vsBytecode.size()) + " psBytes=" + std::to_string(pipeline.psBytecode.size()) + " inputElements=" + std::to_string(pipeline.inputElements.size()));
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

		device2->Release();
		RetirePipelineState(pipeline.psoWithReplacement);
		pipeline.psoWithReplacement = rebuiltPipelineState;
		pipeline.activeShaderTargetName = replacement.name;
		pipeline.activeShaderTargetType = shaderType;
		pipeline.activeShaderTargetHash = shaderHash;
		pipeline.activeShaderTargetUsesFallback = usedFallback;
		RegisterKnownPipelineStateLocked(pipeline.psoWithReplacement);
		if (!rootSignatureOverride)
			PersistAppliedStreamPipelineTemplate(replacement, pipeline, -1, shaderType, shaderHash);
		gPipelineStateOverridesDirty = true;
		ShaderInjectorGUI::WriteToRuntimeLog(
			"HookD3D12->RebuildStreamPSOWithReplacement: Applied stream shader replacement: " +
			replacement.name +
			" modifiedShader=" + replacement.modifiedShaderId +
			" originalPSO=" + StringHelper::PointerToString(pipeline.pipelineState) +
			" root=" + StringHelper::PointerToString(rootSignatureOverride ? rootSignatureOverride : pipeline.rootSignature) +
			" viewInstances=" + std::to_string(pipeline.viewInstanceLocations.size()) +
			(usedFallback ? " (null shader fallback)" : "") +
			" durationMs=" + std::to_string(rebuildDurationMs));
		return true;
	}

	ShaderTargetApplyResult TryApplyGraphicsReplacement(GraphicsPipelineInfo& pipeline)
	{
		struct Candidate { uint64_t hash; ShaderTarget::ShaderType type; };
		const Candidate candidates[] =
		{
			{ pipeline.vsHash, ShaderTarget::VertexShader },
			{ pipeline.psHash, ShaderTarget::PixelShader },
			{ pipeline.gsHash, ShaderTarget::GeometryShader },
			{ pipeline.hsHash, ShaderTarget::HullShader },
			{ pipeline.dsHash, ShaderTarget::DomainShader },
		};

		for (const Candidate& candidate : candidates)
		{
			const int replacementIndex = FindEnabledShaderTarget(candidate.hash, candidate.type);

			if (replacementIndex >= 0)
			{
				return RebuildGraphicsPSOWithReplacement(
					pipeline,
					replacementIndex,
					candidate.hash,
					candidate.type)
					? ShaderTargetApplyResult::Applied
					: ShaderTargetApplyResult::RetryableFailure;
			}
		}

		return ShaderTargetApplyResult::NoMatch;
	}

	ShaderTargetApplyResult TryApplyStreamReplacement(PipelineStateInfo& pipeline)
	{
		struct Candidate { uint64_t hash; ShaderTarget::ShaderType type; };
		const Candidate candidates[] =
		{
			{ pipeline.vsHash, ShaderTarget::VertexShader },
			{ pipeline.psHash, ShaderTarget::PixelShader },
			{ pipeline.csHash, ShaderTarget::ComputeShader },
			{ pipeline.gsHash, ShaderTarget::GeometryShader },
			{ pipeline.hsHash, ShaderTarget::HullShader },
			{ pipeline.dsHash, ShaderTarget::DomainShader },
		};

		for (const Candidate& candidate : candidates)
		{
			const int replacementIndex = FindEnabledShaderTarget(candidate.hash, candidate.type);

			if (replacementIndex >= 0)
			{
				return RebuildStreamPSOWithReplacement(
					pipeline,
					replacementIndex,
					candidate.hash,
					candidate.type)
					? ShaderTargetApplyResult::Applied
					: ShaderTargetApplyResult::RetryableFailure;
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

		const std::string templateLogSuffix = selectedTemplateName.empty() ? std::string() : (" template=" + selectedTemplateName + " matchBytes=" + std::to_string((size_t)selectedTemplateMatchingBytes));

		PipelineStateInfo persistedPipeline{};

		if (!LoadPersistedStreamTemplateFromReplacement(templateReplacement, persistedPipeline))
			return false;

		ID3D12RootSignature* observedRootSignature = nullptr;
		const bool preferComputeRootSignature = shaderType == ShaderTarget::ComputeShader || (persistedPipeline.isCompute && !persistedPipeline.isGraphics);
		
		if (preferComputeRootSignature)
			observedRootSignature = uncaptured.observedComputeRootSignature ? uncaptured.observedComputeRootSignature : uncaptured.observedGraphicsRootSignature;
		else
			observedRootSignature = uncaptured.observedGraphicsRootSignature ? uncaptured.observedGraphicsRootSignature : uncaptured.observedComputeRootSignature;

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

			uncaptured.replacementPipelineState = persistedPipeline.psoWithReplacement;
			uncaptured.activeShaderTargetName = replacement.name;
			uncaptured.activeShaderTargetType = shaderType;
			uncaptured.activeShaderTargetHash = shaderHash;
			gPipelineStateOverrides[uncaptured.pipelineState] = persistedPipeline.psoWithReplacement;
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

		if (replacementIndex < 0 && SupportsCachedBlobContentMatching(uncaptured.cachedBlobSize))
		{
			std::vector<uint8_t> cachedBlob;
			uint64_t currentCachedBlobHash = 0;
			SIZE_T currentCachedBlobSize = 0;
			const bool loadedCachedBlob = GetPipelineCachedBlobInfo(
				uncaptured.pipelineState,
				currentCachedBlobHash,
				currentCachedBlobSize,
				&cachedBlob);

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
			uncaptured.attemptedReplacement = true;
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
					uncaptured.replacementPipelineState = pipeline.psoWithReplacement;
					uncaptured.activeShaderTargetName = replacement.name;
					uncaptured.activeShaderTargetType = replacement.shaderType;
					uncaptured.activeShaderTargetHash = shaderHash;
					gPipelineStateOverrides[uncaptured.pipelineState] = pipeline.psoWithReplacement;
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
					uncaptured.replacementPipelineState = pipeline.psoWithReplacement;
					uncaptured.activeShaderTargetName = replacement.name;
					uncaptured.activeShaderTargetType = replacement.shaderType;
					uncaptured.activeShaderTargetHash = shaderHash;
					gPipelineStateOverrides[uncaptured.pipelineState] = pipeline.psoWithReplacement;
					ShaderInjectorGUI::WriteToRuntimeLog(std::string("HookD3D12->TryApplyUncapturedReplacement: Applied uncaptured PSO replacement by ") + matchMethod + ": " + replacement.name);
					return true;
				}
			}
		}

		for (auto& pipeline : gGraphicsPipelines)
		{
			if (GraphicsPipelineMatchesReplacementTemplate(pipeline, replacement) && RebuildGraphicsPSOWithReplacement(pipeline, replacementIndex, shaderHash, replacement.shaderType))
			{
				uncaptured.replacementPipelineState = pipeline.psoWithReplacement;
				uncaptured.activeShaderTargetName = replacement.name;
				uncaptured.activeShaderTargetType = replacement.shaderType;
				uncaptured.activeShaderTargetHash = shaderHash;
				gPipelineStateOverrides[uncaptured.pipelineState] = pipeline.psoWithReplacement;
				ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->TryApplyUncapturedReplacement: Applied uncaptured PSO replacement by matching graphics template: " + replacement.name);
				return true;
			}
		}

		for (auto& pipeline : gPipelineStates)
		{
			if (StreamPipelineMatchesReplacementTemplate(pipeline, replacement) && RebuildStreamPSOWithReplacement(pipeline, replacementIndex, shaderHash, replacement.shaderType))
			{
				uncaptured.replacementPipelineState = pipeline.psoWithReplacement;
				uncaptured.activeShaderTargetName = replacement.name;
				uncaptured.activeShaderTargetType = replacement.shaderType;
				uncaptured.activeShaderTargetHash = shaderHash;
				gPipelineStateOverrides[uncaptured.pipelineState] = pipeline.psoWithReplacement;
				ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->TryApplyUncapturedReplacement: Applied uncaptured PSO replacement by matching stream template: " + replacement.name);
				return true;
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

		while (!capturedReplacementAttempted && !gUncapturedShaderTargetRetryQueue.empty())
		{
			const size_t pipelineIndex = gUncapturedShaderTargetRetryQueue.front();
			gUncapturedShaderTargetRetryQueue.pop_front();
			if (pipelineIndex >= gUncapturedPipelineStates.size())
				continue;

			UncapturedPipelineStateInfo& uncaptured = gUncapturedPipelineStates[pipelineIndex];
			uncaptured.shaderTargetApplyRetryQueued = false;
			uncaptured.attemptedReplacement = false;
			uncaptured.retryReplacementOnRootSignatureChange = false;
			const bool applied = TryApplyUncapturedReplacement(uncaptured);
			capturedReplacementAttempted = true;
			if (applied)
			{
				ResetShaderTargetRetryState(uncaptured);
				gPipelineStateOverridesDirty.store(true, std::memory_order_release);
			}
			else if (uncaptured.retryReplacementOnRootSignatureChange)
			{
				QueueShaderTargetRetry(
					uncaptured,
					gUncapturedShaderTargetRetryQueue,
					pipelineIndex,
					"uncaptured");
			}
			else
			{
				ResetShaderTargetRetryState(uncaptured);
				if (uncaptured.attemptedReplacement)
					RegisterKnownPipelineStateLocked(uncaptured.pipelineState);
			}
		}

		int uncapturedAttemptsThisFrame = 0;
		while (!capturedReplacementAttempted &&
			gUncapturedShaderTargetApplyCursor < gUncapturedPipelineStates.size() &&
			uncapturedAttemptsThisFrame < gMaximumUncapturedReplacementAttemptsPerFrame)
		{
			auto& uncaptured = gUncapturedPipelineStates[gUncapturedShaderTargetApplyCursor];
			++gUncapturedShaderTargetApplyCursor;

			if (uncaptured.replacementPipelineState || uncaptured.attemptedReplacement)
				continue;

			const bool applied = TryApplyUncapturedReplacement(uncaptured);
			if (uncaptured.replacementPipelineState)
				gPipelineStateOverridesDirty.store(true, std::memory_order_release);
			if (applied)
				ResetShaderTargetRetryState(uncaptured);
			else if (uncaptured.retryReplacementOnRootSignatureChange)
			{
				QueueShaderTargetRetry(
					uncaptured,
					gUncapturedShaderTargetRetryQueue,
					gUncapturedShaderTargetApplyCursor - 1,
					"uncaptured");
			}

			// Once an uncaptured PSO either has an override or has conclusively failed
			// to match, it no longer needs discovery/root-signature synchronization on
			// every bind. Failed persisted rebuilds stay unresolved so a later root
			// signature can still trigger the targeted retry path.
			if (uncaptured.replacementPipelineState ||
				(uncaptured.attemptedReplacement && !uncaptured.retryReplacementOnRootSignatureChange))
			{
				RegisterKnownPipelineStateLocked(uncaptured.pipelineState);
			}
			++uncapturedAttemptsThisFrame;
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
	}

	void SetRuntimeReady(bool ready)
	{
		gRuntimeReady.store(ready, std::memory_order_release);
	}

	ID3D12Device* GetCapturedDevice()
	{
		// The injector owns this reference for the lifetime of the active D3D12 hook.
		// Callers must treat the returned pointer as borrowed.
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
				gSwapChainCommandQueueBindings.push_back({ swapChain, commandQueue });
				registeredNewBinding = true;
			}
		}

		if (registeredNewBinding)
		{
			ShaderInjectorIO::WriteToLogFile(StringHelper::Format(
				"HookD3D12->RegisterSwapChainCommandQueue: swapChain=%p exactDirectQueue=%p",
				swapChain,
				commandQueue));
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
				// Fence values and allocator ownership already reference the previous
				// queue. Changing queues after submission would make that synchronization
				// ambiguous, so disable only the overlay and leave shader hooks active.
				if (gOverlaySubmissionCount != 0)
				{
					ShaderInjectorIO::WriteToLogFileError(StringHelper::Format(
						"HookD3D12->AdoptMostRecentDirectCommandQueue: swap-chain queue changed after overlay submission old=%p new=%p; overlay disabled",
						gCommandQueue,
						exactCommandQueue));
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
		const HRESULT removedReason = gDevice ? gDevice->GetDeviceRemovedReason() : E_POINTER;
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

		for (auto& req : gPendingRebuilds)
		{
			if (req.source == PSOPendingRebuild::SourceList::Graphics)
			{
				if (req.index >= (int)gGraphicsPipelines.size())
					continue;

				auto& p = gGraphicsPipelines[req.index];

				if (!p.psoWithoutPS && gDevice)
				{
					//IMPORTANT NOTE: This executes
					//MessageBoxA(nullptr, "Starting Graphics Pipeline State Rebuild...", "Shader Injector", MB_OK);

					//NOTE: rebuild!
					D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = p.originalDesc;
					desc.VS = { p.vsBytecode.empty() ? nullptr : p.vsBytecode.data(), p.vsBytecode.size() };

					const bool hiddenSelection = gShaderSelectionStyle == PixelShaderSelectionStyle::Hidden;
					const std::vector<uint8_t>& markerBlob = !Globals::markerPixelShaderBlob.empty() ? Globals::markerPixelShaderBlob : Globals::nullPixelShaderBlob;
					desc.PS = hiddenSelection || markerBlob.empty() ? D3D12_SHADER_BYTECODE{ nullptr, 0 } : D3D12_SHADER_BYTECODE{ markerBlob.data(), markerBlob.size() };
					
					desc.InputLayout.pInputElementDescs = p.inputElements.empty() ? nullptr : p.inputElements.data();
					desc.InputLayout.NumElements = (UINT)p.inputElements.size();
					desc.StreamOutput.pSODeclaration = p.soDeclarations.empty() ? nullptr : p.soDeclarations.data();
					desc.StreamOutput.NumEntries = (UINT)p.soDeclarations.size();
					desc.StreamOutput.pBufferStrides = p.soStrides.empty() ? nullptr : p.soStrides.data();
					desc.StreamOutput.NumStrides = (UINT)p.soStrides.size();
					desc.CachedPSO = { nullptr, 0 };

					//EXTREMELY IMPORTANT NOTE: we call the original function here, not through device-> otherwise we create a recursion loop and create a fatal application error
					//HRESULT hr = gDevice->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&p.psoWithoutPS)); //<-- causes recursion madness
					HRESULT hr = Original_CreateGraphicsPipelineState(gDevice, &desc, IID_PPV_ARGS(&p.psoWithoutPS));

					if (SUCCEEDED(hr))
						RegisterKnownPipelineStateLocked(p.psoWithoutPS);

					if (FAILED(hr))
					{
						p.psDisabled = false;
						
						//char errbuf[128];
						//sprintf_s(errbuf, "hr=0x%08X", (unsigned)hr);
						//PrintLog("ProcessPendingRebuilds: graphics PSO rebuild failed " + std::string(errbuf));

						// Dump desc fields to diagnose E_INVALIDARG
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
							desc.BlendState.RenderTarget[0].BlendEnable
						);

						MessageBoxA(nullptr, dbg, "PSO Rebuild Desc", MB_OK);
					}
				}
			}
			else if (req.source == PSOPendingRebuild::SourceList::Stream)
			{
				if (req.index >= (int)gPipelineStates.size())
					continue;

				auto& p = gPipelineStates[req.index];

				ID3D12PipelineState** outPSO = nullptr;

				switch (req.targetType)
				{
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS: outPSO = &p.psoWithoutVS; break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS: outPSO = &p.psoWithoutPS; break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS: outPSO = &p.psoWithoutCS; break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS: outPSO = &p.psoWithoutGS; break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS: outPSO = &p.psoWithoutHS; break;
					case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS: outPSO = &p.psoWithoutDS; break;
					default: continue;
				}

				if (outPSO && !*outPSO)
					RebuildStreamPSOWithoutStage(p, req.targetType, *outPSO, gDevice);
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

			// Another overlay may have replaced our table after installation. Only
			// restore the pointer when this object still owns the active table.
			if (currentVTable == gRTSSCompatibilitySwapChainVTable)
			{
				InterlockedExchangePointer(
					reinterpret_cast<PVOID volatile*>(gRTSSCompatibilitySwapChain),
					gRTSSOriginalSwapChainVTable);
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

	bool InstallSwapChainCompatibility(IDXGISwapChain3* pSwapChain, const char* compatibilitySource)
	{
		if (!pSwapChain)
			return false;

		std::lock_guard<std::mutex> lock(gRTSSCompatibilityMutex);
		void** currentVTable = *reinterpret_cast<void***>(pSwapChain);

		if (pSwapChain == gRTSSCompatibilitySwapChain && currentVTable == gRTSSCompatibilitySwapChainVTable)
			return true;

		RestoreRTSSSwapChainCompatibilityLocked();
		currentVTable = *reinterpret_cast<void***>(pSwapChain);

		void** compatibilityVTable = new void*[gSwapChain3VTableEntryCount];
		std::memcpy(
			compatibilityVTable,
			currentVTable,
			gSwapChain3VTableEntryCount * sizeof(void*));

		FunctionPresentD3D12 currentPresent =
			reinterpret_cast<FunctionPresentD3D12>(currentVTable[VTableIndex::indexPresent]);
		FunctionPresent1D3D12 currentPresent1 =
			reinterpret_cast<FunctionPresent1D3D12>(currentVTable[VTableIndex::indexPresent1]);
		FunctionResizeBuffersD3D12 currentResizeBuffers =
			reinterpret_cast<FunctionResizeBuffersD3D12>(currentVTable[VTableIndex::indexResizeBuffers]);

		compatibilityVTable[VTableIndex::indexPresent] =
			reinterpret_cast<void*>(&Hook_RTSSCompatibilityPresent);
		compatibilityVTable[VTableIndex::indexPresent1] =
			reinterpret_cast<void*>(&Hook_RTSSCompatibilityPresent1);
		compatibilityVTable[VTableIndex::indexResizeBuffers] =
			reinterpret_cast<void*>(&Hook_RTSSCompatibilityResizeBuffers);

		// Publish every downstream pointer before making the private table visible
		// to other threads that may already be presenting this swap chain.
		pSwapChain->AddRef();
		gRTSSCompatibilitySwapChain = pSwapChain;
		gRTSSOriginalSwapChainVTable = currentVTable;
		gRTSSCompatibilitySwapChainVTable = compatibilityVTable;
		gRTSSOriginalPresent = currentPresent;
		gRTSSOriginalPresent1 = currentPresent1;
		gRTSSOriginalResizeBuffers = currentResizeBuffers;

		InterlockedExchangePointer(
			reinterpret_cast<PVOID volatile*>(pSwapChain),
			compatibilityVTable);

		ShaderInjectorIO::WriteToLogFile(StringHelper::Format(
			"HookD3D12->InstallSwapChainCompatibility: installed source=%s swapChain=%p Present=%p Present1=%p ResizeBuffers=%p",
			compatibilitySource ? compatibilitySource : "External",
			pSwapChain,
			reinterpret_cast<void*>(currentPresent),
			reinterpret_cast<void*>(currentPresent1),
			reinterpret_cast<void*>(currentResizeBuffers)));

		return true;
	}

	bool InstallRTSSSwapChainCompatibility(IDXGISwapChain3* pSwapChain)
	{
		if (!FindRTSSHookModule())
			return false;

		return InstallSwapChainCompatibility(pSwapChain, "RTSS");
	}

	void Release()
	{
		//ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->Release: Releasing resources and hooks.");

		gRuntimeReady.store(false, std::memory_order_release);
		gShutdown = true;
		ShaderAutomaticDiscovery::Shutdown();
		ResetOverlayStartupGate();

		{
			std::lock_guard<std::mutex> lock(gRTSSCompatibilityMutex);
			RestoreRTSSSwapChainCompatibilityLocked();
		}

		if (Globals::mainWindow)
		{
			//inputhook::Remove(globals::mainWindow);
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

		// Disable hooks installed for D3D12
		//hooks::Remove();
	}

	bool IsInitialized()
	{
		return gInitialized;
	}
}
