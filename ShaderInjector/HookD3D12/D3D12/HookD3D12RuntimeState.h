#pragma once

#include <atomic>
#include <mutex>
#include <unordered_map>

#include "../HookD3D12.h"

namespace HookD3D12
{
	struct CommandListPipelineState
	{
		std::atomic<ID3D12RootSignature*> graphicsRootSignature = nullptr;
		std::atomic<ID3D12RootSignature*> computeRootSignature = nullptr;
		std::atomic<ID3D12PipelineState*> pipelineState = nullptr;
	};

	extern std::atomic<uint32_t> gActivePipelineActivityCount;
	extern thread_local bool gInsideOverlayResourceCreation;
	extern std::mutex gPipelineMutex;
	extern std::vector<UncapturedPipelineStateInfo> gUncapturedPipelineStates;
	extern std::unordered_map<ID3D12PipelineState*, size_t> gUncapturedPipelineStateIndexByPointer;
	extern std::unordered_map<ID3D12PipelineState*, ID3D12PipelineState*> gPipelineStateOverrides;
	extern std::atomic<bool> gPipelineStateOverridesDirty;
	extern ID3D12Device* gDevice;
	extern ID3D12Device* gDevice2;
	extern ID3D12CommandQueue* gCommandQueue;
	extern ID3D12DescriptorHeap* gHeapRTV;
	extern ID3D12DescriptorHeap* gHeapSRV;
	extern ID3D12GraphicsCommandList* gCommandList;
	extern ID3D12Fence* gOverlayFence;
	extern HANDLE gFenceEvent;
	extern UINT64 gOverlayFenceValue;
	extern UINT gBufferCount;
	extern FrameContext* gFrameContexts;
	extern bool gInitialized;
	extern bool gShutdown;
	extern ULONGLONG gOverlayInitializedTick;
	extern bool gLoggedStartupMenuDelay;
	extern bool gOverlayRenderingDisabled;
	extern bool gOverlayDeviceObjectsCreated;
	extern bool gLoggedPresentHook;
	extern bool gLoggedPresent1Hook;
	extern bool gLoggedOverlayInitialized;
	extern bool gLoggedOverlayPipelineActivityDelay;
	extern UINT64 gOverlaySubmissionCount;
	extern FunctionPresentD3D12 gRTSSOriginalPresent;
	extern FunctionPresent1D3D12 gRTSSOriginalPresent1;
	extern FunctionResizeBuffersD3D12 gRTSSOriginalResizeBuffers;
	extern std::mutex gRTSSCompatibilityMutex;
	extern thread_local bool gInsideSwapChainCompatibilityCall;
	extern std::atomic<bool> gRuntimeReady;

	class ScopedSwapChainCompatibilityCall
	{
	public:
		ScopedSwapChainCompatibilityCall()
		{
			gInsideSwapChainCompatibilityCall = true;
		}

		~ScopedSwapChainCompatibilityCall()
		{
			gInsideSwapChainCompatibilityCall = false;
		}
	};

	CommandListPipelineState& GetCommandListPipelineState(ID3D12GraphicsCommandList* commandList);
	void UpdateUncapturedPipelineRootSignatureLocked(ID3D12PipelineState* pipelineState, ID3D12RootSignature* rootSignature, bool computeRootSignature);
	void RecordUncapturedPipelineStateLocked(ID3D12PipelineState* pipelineState, ID3D12RootSignature* observedGraphicsRootSignature, ID3D12RootSignature* observedComputeRootSignature, const char* reason);
	bool TryResolvePublishedPipelineState(ID3D12PipelineState* requestedPipelineState, ID3D12PipelineState*& resolvedPipelineState);
	void RebuildPipelineStateOverrideMap();
	void RememberDirectCommandQueue(ID3D12CommandQueue* commandQueue);
	bool AdoptMostRecentDirectCommandQueue(IDXGISwapChain3* swapChain);
	void ProcessPendingRebuilds();
	void GatherPipelineInfo(IDXGISwapChain3* swapChain);
	void ApplyShaderTargetPSOs();
	void InstallPipelineHooks();
	void InstallCommandListHooks();
	void LogOverlayDeviceFailure(const char* operation, HRESULT result);
	bool InstallSwapChainCompatibility(IDXGISwapChain3* swapChain, const char* compatibilitySource);
	bool InstallRTSSSwapChainCompatibility(IDXGISwapChain3* swapChain);
	bool WaitForOverlayGPUIdle(DWORD timeoutMilliseconds = 2000);
	void ReleaseOverlaySwapChainResources(bool shutdownImGuiBackend);
}
