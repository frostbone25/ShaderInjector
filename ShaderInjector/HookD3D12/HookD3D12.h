#pragma once

#include <cstdint>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <wrl/client.h>

#include "Enum/PipelineSourceList.h"
#include "Enum/PixelShaderSelectionStyle.h"
#include "ShaderTarget/ShaderTarget.h"
#include "HookD3D12/ComputePipelineInfo.h"
#include "HookD3D12/D3D12PipelineInfo.h"
#include "HookD3D12/FrameContext.h"
#include "HookD3D12/GraphicsPipelineInfo.h"
#include "HookD3D12/PSOPendingRebuild.h"
#include "HookD3D12/PipelineStateInfo.h"
#include "HookD3D12/RootSignatureInfo.h"
#include "HookD3D12/UncapturedPipelineStateInfo.h"
#include "HookD3D12/HookD3D12RenderPass.h"
#include "HookD3D12/HookD3D12Resources.h"

namespace HookD3D12
{
	// Return the device-specific spacing between descriptors in a heap.
	UINT DescriptorIncrementSize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType);

	// Track resources created through a hooked device call when creation succeeds.
	void RegisterCreatedResource(HRESULT result, void** createdObject);

	// Install hooks as their corresponding device, command list, or swap chain becomes available.
	bool InstallD3D12CreateDeviceHook(HMODULE d3d12Module);
	void InstallPipelineHooksForDevice(ID3D12Device* device);
	void InstallRenderPassResourceHooksForDevice(ID3D12Device* device);
	void InstallCommandListHooksForCommandList(ID3D12GraphicsCommandList* commandList);
	void InstallDeferredRenderPassHooks(ID3D12Device* device);
	bool InstallSwapChainCompatibility(IDXGISwapChain3* swapChain, const char* compatibilitySource);
	void RegisterSwapChainCommandQueue(IDXGISwapChain3* swapChain, IUnknown* creationDevice);

	// Expose the live device and whether the hooked renderer is ready for injector work.
	void SetRuntimeReady(bool ready);
	ID3D12Device* GetCapturedDevice();

	// Call the original stream-PSO creation path while building an injector-owned PSO.
	HRESULT CreatePipelineStateInternal(
		ID3D12Device2* device,
		const D3D12_PIPELINE_STATE_STREAM_DESC* description,
		REFIID interfaceId,
		void** pipelineState);

	// Return a borrowed rebuild template; callers must hold gPipelineMutex while using it.
	const PipelineStateInfo* FindUncapturedRebuildTemplateLocked(ID3D12PipelineState* pipelineState);


	// Shared captured-pipeline state. Access to the pipeline collections is coordinated by gPipelineMutex.
	extern std::vector<PSOPendingRebuild> gPendingRebuilds;
	extern std::mutex gPipelineMutex;
	extern std::vector<GraphicsPipelineInfo> gGraphicsPipelines;
	extern std::vector<ComputePipelineInfo> gComputePipelines;
	extern D3D12PipelineInfo gPipelineInfo;
	extern std::vector<PipelineStateInfo> gPipelineStates;
	extern std::vector<ShaderTarget::ShaderTargetDisk> gLoadedShaderTargets;
	extern std::vector<std::vector<uint8_t>> gLoadedShaderTargetBlobs;
	extern int gSelectedShaderTargetIndex;
	extern int gShaderTargetNameBufferIndex;
	extern char gShaderTargetNameBuffer[256];
	extern bool gLoadedShaderTargetsOnce;
	extern PixelShaderSelectionStyle gShaderSelectionStyle;


	// Mark work performed inside a pipeline hook so nested injector calls can be recognized.
	class ScopedPipelineActivity
	{
	public:
		explicit ScopedPipelineActivity(bool trackActivity = true);
		~ScopedPipelineActivity();

		ScopedPipelineActivity(const ScopedPipelineActivity&) = delete;
		ScopedPipelineActivity& operator=(const ScopedPipelineActivity&) = delete;

	private:
		bool trackingActivity = false;
	};


	// Look up replacements and schedule or invalidate PSO rebuild work when state changes.
	int FindEnabledShaderTarget(uint64_t shaderHash, ShaderTarget::ShaderType shaderType);
	void MarkShaderTargetApplyDirty();
	void QueueShaderTargetApplyWork();
	void InvalidateAllReplacementPSOs();
	void ResetUncapturedReplacementAttempts();
	void ClearShaderMarkers();
	void InvalidateShaderMarkerPSOs();

	// Preserve newly created PSOs and shader bytecode for discovery and later rebuilds.
	void CaptureGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pipelineDescription, ID3D12PipelineState* pipelineState);
	void CaptureComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* pipelineDescription, ID3D12PipelineState* pipelineState, bool shouldRegisterAsKnownPipeline);
	void CapturePipelineStateStream(const D3D12_PIPELINE_STATE_STREAM_DESC* pipelineStreamDescription, ID3D12PipelineState* pipelineState);

	// Resolve and retain root signatures needed by generated passes and persisted PSOs.
	bool GetRootSignatureBlob(ID3D12RootSignature* rootSignature, std::vector<uint8_t>& outBlob, uint64_t& outHash);
	void EnsureRenderPassRootSignatureRegistered(ID3D12RootSignature* rootSignature);
	ID3D12RootSignature* GetOrCreatePersistedRootSignature(const ShaderTarget::ShaderTargetDisk& replacement, ID3D12Device* device);
	void ReleaseRootSignatureCache();

	// Manage shader targets loaded from disk and synchronize their UI/runtime state.
	void RefreshLoadedShaderTargets();
	void SyncShaderTargetNameBuffer();
	bool SaveShaderTarget(int index);
	bool CompileShaderTarget(int index);
	bool ReloadShaderTarget(int index);
	bool DeleteShaderTarget(int index);
	bool IsShaderTargetEffectivelyEnabled(const ShaderTarget::ShaderTargetDisk& replacement);
	void RefreshShaderTargetsForModifiedShaderStateChange();

	// Build a shader target from either a graphics descriptor or a pipeline-state stream.
	bool CreateShaderTargetForPipeline(
		const std::string& sourceList,
		int pipelineIndex,
		ShaderTarget::ShaderType shaderType,
		uint64_t shaderHash,
		size_t shaderBytecodeLength,
		const void* shaderBytecode,
		GraphicsPipelineInfo& pipeline,
		const std::string& modifiedShaderId,
		bool generateShaderDisassembly,
		const ShaderAnalysis::ShaderAnalysisDisk* originalShaderAnalysis = nullptr);
	bool CreateShaderTargetForPipeline(
		const std::string& sourceList,
		int pipelineIndex,
		ShaderTarget::ShaderType shaderType,
		uint64_t shaderHash,
		size_t shaderBytecodeLength,
		const void* shaderBytecode,
		PipelineStateInfo& pipeline,
		const std::string& modifiedShaderId,
		bool generateShaderDisassembly,
		const ShaderAnalysis::ShaderAnalysisDisk* originalShaderAnalysis = nullptr);


	// Release captured resources and report whether the hook runtime is active.
	extern void Release();
	bool IsInitialized();

	// Device and pipeline creation
	// Hook_* forwards to Handle_*; Original_* stores the unhooked function pointer.
	// Observe device creation before pipeline and command-list hooks are installed.
	// reference - https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-d3d12createdevice
	typedef HRESULT(WINAPI* FunctionCreateDeviceD3D12)(IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid, void** ppDevice);

	extern FunctionCreateDeviceD3D12 Original_CreateDeviceD3D12;

	HRESULT WINAPI Hook_CreateDeviceD3D12(IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid, void** ppDevice);

	// Record root signatures for compatible replacement PSOs.
	using FunctionCreateRootSignatureD3D12 = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, UINT, const void*, SIZE_T, REFIID, void**);

	extern FunctionCreateRootSignatureD3D12 Original_CreateRootSignature;

	extern HRESULT STDMETHODCALLTYPE Hook_CreateRootSignature(ID3D12Device* device, UINT nodeMask, const void* blobWithRootSignature, SIZE_T blobLengthInBytes, REFIID riid, void** rootSignature);

	// Capture graphics pipeline descriptors and shader bytecode.
	using FunctionCreateGraphicsPipelineStateD3D12 = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, REFIID, void**);

	extern FunctionCreateGraphicsPipelineStateD3D12 Original_CreateGraphicsPipelineState;

	extern HRESULT STDMETHODCALLTYPE Hook_CreateGraphicsPipelineState(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID riid, void** ppPipelineState);

	// Capture compute pipeline descriptors and shader bytecode.
	using FunctionCreateComputePipelineStateD3D12 = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_COMPUTE_PIPELINE_STATE_DESC*, REFIID, void**);

	extern FunctionCreateComputePipelineStateD3D12 Original_CreateComputePipelineState;

	extern HRESULT STDMETHODCALLTYPE Hook_CreateComputePipelineState(ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, REFIID riid, void** ppPipelineState);

	// Capture game-created pipeline-state streams.
	// reference - https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device2-createpipelinestate
	using FunctionCreatePipelineStateD3D12 = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device2*, const D3D12_PIPELINE_STATE_STREAM_DESC*, REFIID, void**);

	extern FunctionCreatePipelineStateD3D12 Original_CreatePipelineState;

	extern HRESULT STDMETHODCALLTYPE Hook_CreatePipelineState(ID3D12Device2* device, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, REFIID riid, void** ppPipelineState);

	// Capture new devices, signatures, and PSOs before returning to the game.
	HRESULT WINAPI Handle_CreateDeviceD3D12(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
	HRESULT STDMETHODCALLTYPE Handle_CreateComputePipelineState(ID3D12Device*, const D3D12_COMPUTE_PIPELINE_STATE_DESC*, REFIID, void**);
	HRESULT STDMETHODCALLTYPE Handle_CreateGraphicsPipelineState(ID3D12Device*, const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, REFIID, void**);
	HRESULT STDMETHODCALLTYPE Handle_CreatePipelineState(ID3D12Device2*, const D3D12_PIPELINE_STATE_STREAM_DESC*, REFIID, void**);
	HRESULT STDMETHODCALLTYPE Handle_CreateRootSignature(ID3D12Device*, UINT, const void*, SIZE_T, REFIID, void**);

	//||||||||||||||||||||||||||||||| PIPELINE LIBRARIES |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| PIPELINE LIBRARIES |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| PIPELINE LIBRARIES |||||||||||||||||||||||||||||||

	// Observe PSOs loaded from or stored in the game's pipeline cache.
	// reference - https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device1-createpipelinelibrary
	// Observe libraries created from cached game data.
	typedef HRESULT(__stdcall* FunctionCreatePipelineLibraryD3D12)(ID3D12Device1*, const void*, SIZE_T, REFIID, void**);

	extern FunctionCreatePipelineLibraryD3D12 Original_CreatePipelineLibrary;

	extern HRESULT __stdcall Hook_CreatePipelineLibrary(ID3D12Device1* device, const void* pLibraryBlob, SIZE_T blobLength, REFIID riid, void** ppPipelineLibrary);

	// Observe graphics PSOs loaded from a pipeline library.
	// reference - https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12pipelinelibrary-loadgraphicspipeline
	typedef HRESULT(__stdcall* FunctionLoadGraphicsPipelineD3D12)(ID3D12PipelineLibrary*, LPCWSTR, const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, REFIID, void**);

	extern FunctionLoadGraphicsPipelineD3D12 Original_LoadGraphicsPipeline;

	HRESULT __stdcall Hook_LoadGraphicsPipeline(ID3D12PipelineLibrary* library, LPCWSTR name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID riid, void** ppPipelineState);

	// Observe compute PSOs loaded from a pipeline library.
	typedef HRESULT(__stdcall* FunctionLoadComputePipelineD3D12)(ID3D12PipelineLibrary*, LPCWSTR, const D3D12_COMPUTE_PIPELINE_STATE_DESC*, REFIID, void**);

	extern FunctionLoadComputePipelineD3D12 Original_LoadComputePipeline;

	extern HRESULT __stdcall Hook_LoadComputePipeline(ID3D12PipelineLibrary* library, LPCWSTR name, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, REFIID riid, void** ppPipelineState);

	// Observe stream PSOs loaded from a pipeline library.
	typedef HRESULT(__stdcall* FunctionLoadPipelineD3D12)(ID3D12PipelineLibrary1*, LPCWSTR, const D3D12_PIPELINE_STATE_STREAM_DESC*, REFIID, void**);

	extern FunctionLoadPipelineD3D12 Original_LoadPipeline;

	extern HRESULT __stdcall Hook_LoadPipeline(ID3D12PipelineLibrary1* library, LPCWSTR name, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, REFIID riid, void** ppPipelineState);

	// Track PSOs stored in the pipeline library.
	typedef HRESULT(__stdcall* FunctionStorePipelineD3D12)(ID3D12PipelineLibrary*, LPCWSTR, ID3D12PipelineState*);

	extern FunctionStorePipelineD3D12 Original_StorePipeline;

	HRESULT __stdcall Hook_StorePipeline(ID3D12PipelineLibrary* library, LPCWSTR name, ID3D12PipelineState* pso);

	// Forward a pipeline library's serialized-size query.
	typedef SIZE_T(__stdcall* FunctionGetSerializedSizeD3D12)(ID3D12PipelineLibrary*);

	extern FunctionGetSerializedSizeD3D12 Original_GetSerializedSize;

	extern SIZE_T __stdcall Hook_GetSerializedSize(ID3D12PipelineLibrary* library);

	// Forward pipeline library serialization.
	typedef HRESULT(__stdcall* FunctionSerializeD3D12)(ID3D12PipelineLibrary*, void*, SIZE_T);

	extern FunctionSerializeD3D12 Original_Serialize;

	extern HRESULT __stdcall Hook_Serialize(ID3D12PipelineLibrary* library, void* data, SIZE_T dataSize);

	// Install library-specific hooks and process their captured calls.
	HRESULT __stdcall Handle_CreatePipelineLibrary(ID3D12Device1*, const void*, SIZE_T, REFIID, void**);
	HRESULT __stdcall Handle_LoadGraphicsPipeline(ID3D12PipelineLibrary*, LPCWSTR, const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, REFIID, void**);
	HRESULT __stdcall Handle_LoadComputePipeline(ID3D12PipelineLibrary*, LPCWSTR, const D3D12_COMPUTE_PIPELINE_STATE_DESC*, REFIID, void**);
	HRESULT __stdcall Handle_LoadPipeline(ID3D12PipelineLibrary1*, LPCWSTR, const D3D12_PIPELINE_STATE_STREAM_DESC*, REFIID, void**);
	HRESULT __stdcall Handle_StorePipeline(ID3D12PipelineLibrary*, LPCWSTR, ID3D12PipelineState*);
	SIZE_T __stdcall Handle_GetSerializedSize(ID3D12PipelineLibrary*);
	HRESULT __stdcall Handle_Serialize(ID3D12PipelineLibrary*, void*, SIZE_T);
	void HookPipelineLibrary(ID3D12PipelineLibrary*);

	// Swap chains and presentation
	// Associate each D3D12 swap chain with its direct command queue and manage overlay resources.
	// reference - https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforhwnd
	using FunctionCreateSwapChain = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
	using FunctionCreateSwapChainForHwnd = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);

	extern FunctionCreateSwapChain gOriginalCreateSwapChain;
	extern FunctionCreateSwapChainForHwnd gOriginalCreateSwapChainForHwnd;

	// Remember the command queue that DXGI received when it created this swap chain.
	void CaptureCreatedSwapChain(IUnknown* creationDevice, IUnknown* swapChain);

	// reference - https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgifactory-createswapchain
	HRESULT STDMETHODCALLTYPE Hook_CreateSwapChain(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
	HRESULT STDMETHODCALLTYPE Hook_CreateSwapChainForHwnd(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);

	HRESULT STDMETHODCALLTYPE Handle_CreateSwapChain(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
	HRESULT STDMETHODCALLTYPE Handle_CreateSwapChainForHwnd(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);

	// Draw the overlay while preserving the game's present call.
	// reference - https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present
	typedef HRESULT(STDMETHODCALLTYPE* FunctionPresentD3D12)(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags);
	typedef HRESULT(STDMETHODCALLTYPE* FunctionPresent1D3D12)(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pParams);

	extern FunctionPresentD3D12 Original_PresentD3D12;
	extern FunctionPresent1D3D12 Original_Present1D3D12;

	extern HRESULT STDMETHODCALLTYPE Hook_PresentD3D12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags);
	extern HRESULT STDMETHODCALLTYPE Hook_Present1D3D12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pParams);

	// Release and recreate swap-chain-dependent resources around a resize.
	// reference - https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers
	typedef HRESULT(STDMETHODCALLTYPE* FunctionResizeBuffersD3D12)(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);

	extern FunctionResizeBuffersD3D12 Original_ResizeBuffersD3D12;

	extern HRESULT STDMETHODCALLTYPE Hook_ResizeBuffersD3D12(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);

	// RTSS compatibility callbacks and presentation/resize handlers run on the same swap chain.
	HRESULT STDMETHODCALLTYPE Handle_PresentD3D12(IDXGISwapChain3*, UINT, UINT);
	HRESULT STDMETHODCALLTYPE Handle_Present1D3D12(IDXGISwapChain3*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
	HRESULT STDMETHODCALLTYPE Hook_RTSSCompatibilityPresent(IDXGISwapChain3*, UINT, UINT);
	HRESULT STDMETHODCALLTYPE Hook_RTSSCompatibilityPresent1(IDXGISwapChain3*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
	HRESULT STDMETHODCALLTYPE Hook_RTSSCompatibilityResizeBuffers(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
	HRESULT STDMETHODCALLTYPE Handle_RTSSCompatibilityPresent(IDXGISwapChain3*, UINT, UINT);
	HRESULT STDMETHODCALLTYPE Handle_RTSSCompatibilityPresent1(IDXGISwapChain3*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
	HRESULT STDMETHODCALLTYPE Handle_ResizeBuffersD3D12(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
	HRESULT STDMETHODCALLTYPE Handle_RTSSCompatibilityResizeBuffers(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

	// Command queue and PSO binding
	// Track submissions, command-list resets, pipeline binds, and root signatures.
	// Observe command lists submitted to the GPU.
	// reference - https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists
	typedef void(STDMETHODCALLTYPE* FunctionExecuteCommandListsD3D12)(ID3D12CommandQueue* _this, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists);

	extern FunctionExecuteCommandListsD3D12 Original_ExecuteCommandListsD3D12;

	extern void STDMETHODCALLTYPE Hook_ExecuteCommandListsD3D12(ID3D12CommandQueue* _this, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists);

	// Track the initial PSO when the game resets a command list.
	using FunctionResetGraphicsCommandListD3D12 = HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);

	extern FunctionResetGraphicsCommandListD3D12 Original_ResetGraphicsCommandList;

	extern HRESULT STDMETHODCALLTYPE Hook_ResetGraphicsCommandList(ID3D12GraphicsCommandList* cmdList, ID3D12CommandAllocator* allocator, ID3D12PipelineState* initialState);

	// Apply the replacement PSO when the game binds its original PSO.
	// reference - https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setpipelinestate
	using FunctionSetPipelineStateD3D12 = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);

	extern FunctionSetPipelineStateD3D12 Original_SetPipelineState;

	extern void STDMETHODCALLTYPE Hook_SetPipelineState(ID3D12GraphicsCommandList* cmdList, ID3D12PipelineState* pso);

	// Track the compute root signature bound to a command list.
	using FunctionSetComputeRootSignatureD3D12 = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12RootSignature*);

	extern FunctionSetComputeRootSignatureD3D12 Original_SetComputeRootSignature;

	extern void STDMETHODCALLTYPE Hook_SetComputeRootSignature(ID3D12GraphicsCommandList* cmdList, ID3D12RootSignature* rootSignature);

	// Track the graphics root signature bound to a command list.
	using FunctionSetGraphicsRootSignatureD3D12 = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12RootSignature*);

	extern FunctionSetGraphicsRootSignatureD3D12 Original_SetGraphicsRootSignature;

	extern void STDMETHODCALLTYPE Hook_SetGraphicsRootSignature(ID3D12GraphicsCommandList* cmdList, ID3D12RootSignature* rootSignature);

	// Update command-list state around the original game calls.
	void STDMETHODCALLTYPE Handle_SetPipelineState(ID3D12GraphicsCommandList*, ID3D12PipelineState*);
	HRESULT STDMETHODCALLTYPE Handle_ResetGraphicsCommandList(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);
	void STDMETHODCALLTYPE Handle_SetGraphicsRootSignature(ID3D12GraphicsCommandList*, ID3D12RootSignature*);
	void STDMETHODCALLTYPE Handle_SetComputeRootSignature(ID3D12GraphicsCommandList*, ID3D12RootSignature*);
	void STDMETHODCALLTYPE Handle_ExecuteCommandListsD3D12(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

	// Device resources and descriptors
	// Track resources and views that can later be inherited by a render pass.
	// reference - https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createshaderresourceview
	// Original_* function types and pointers are declared in HookD3D12Resources.h.
	// Create heaps and views so later passes can identify the resources behind descriptors.
	HRESULT STDMETHODCALLTYPE Hook_CreateDescriptorHeap(ID3D12Device*, const D3D12_DESCRIPTOR_HEAP_DESC*, REFIID, void**);
	void STDMETHODCALLTYPE Hook_CreateConstantBufferView(ID3D12Device*, const D3D12_CONSTANT_BUFFER_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Hook_CreateShaderResourceView(ID3D12Device*, ID3D12Resource*, const D3D12_SHADER_RESOURCE_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Hook_CreateUnorderedAccessView(ID3D12Device*, ID3D12Resource*, ID3D12Resource*, const D3D12_UNORDERED_ACCESS_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Hook_CreateRenderTargetView(ID3D12Device*, ID3D12Resource*, const D3D12_RENDER_TARGET_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Hook_CreateDepthStencilView(ID3D12Device*, ID3D12Resource*, const D3D12_DEPTH_STENCIL_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Hook_CreateSampler(ID3D12Device*, const D3D12_SAMPLER_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);

	// Follow descriptor copies made after the original views were created.
	void STDMETHODCALLTYPE Hook_CopyDescriptors(ID3D12Device*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*, D3D12_DESCRIPTOR_HEAP_TYPE);
	void STDMETHODCALLTYPE Hook_CopyDescriptorsSimple(ID3D12Device*, UINT, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_DESCRIPTOR_HEAP_TYPE);

	// Record backing resource creation and its initial state.
	HRESULT STDMETHODCALLTYPE Hook_CreateCommittedResource(ID3D12Device*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
	HRESULT STDMETHODCALLTYPE Hook_CreatePlacedResource(ID3D12Device*, ID3D12Heap*, UINT64, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
	HRESULT STDMETHODCALLTYPE Hook_CreateReservedResource(ID3D12Device*, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);

	// Register created resources and copy descriptor metadata.
	HRESULT STDMETHODCALLTYPE Handle_CreateDescriptorHeap(ID3D12Device*, const D3D12_DESCRIPTOR_HEAP_DESC*, REFIID, void**);
	void STDMETHODCALLTYPE Handle_CreateConstantBufferView(ID3D12Device*, const D3D12_CONSTANT_BUFFER_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Handle_CreateShaderResourceView(ID3D12Device*, ID3D12Resource*, const D3D12_SHADER_RESOURCE_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Handle_CreateUnorderedAccessView(ID3D12Device*, ID3D12Resource*, ID3D12Resource*, const D3D12_UNORDERED_ACCESS_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Handle_CreateRenderTargetView(ID3D12Device*, ID3D12Resource*, const D3D12_RENDER_TARGET_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Handle_CreateDepthStencilView(ID3D12Device*, ID3D12Resource*, const D3D12_DEPTH_STENCIL_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Handle_CreateSampler(ID3D12Device*, const D3D12_SAMPLER_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Handle_CopyDescriptors(ID3D12Device*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*, D3D12_DESCRIPTOR_HEAP_TYPE);
	void STDMETHODCALLTYPE Handle_CopyDescriptorsSimple(ID3D12Device*, UINT, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_DESCRIPTOR_HEAP_TYPE);
	HRESULT STDMETHODCALLTYPE Handle_CreateCommittedResource(ID3D12Device*, const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
	HRESULT STDMETHODCALLTYPE Handle_CreatePlacedResource(ID3D12Device*, ID3D12Heap*, UINT64, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
	HRESULT STDMETHODCALLTYPE Handle_CreateReservedResource(ID3D12Device*, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);

	// Command recording for render passes
	// Observe draws, dispatches, state changes, and resource bindings at execution boundaries.
	// reference - https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-drawinstanced
	// Original_* function types and pointers are declared in HookD3D12RenderPass.h.
	// Draw and dispatch boundaries determine when configured passes execute.
	void STDMETHODCALLTYPE Hook_DrawInstanced(ID3D12GraphicsCommandList*, UINT, UINT, UINT, UINT);
	void STDMETHODCALLTYPE Hook_DrawIndexedInstanced(ID3D12GraphicsCommandList*, UINT, UINT, UINT, INT, UINT);
	void STDMETHODCALLTYPE Hook_Dispatch(ID3D12GraphicsCommandList*, UINT, UINT, UINT);

	// Preserve raster and input-assembly state for the active draw.
	void STDMETHODCALLTYPE Hook_IASetPrimitiveTopology(ID3D12GraphicsCommandList*, D3D12_PRIMITIVE_TOPOLOGY);
	void STDMETHODCALLTYPE Hook_RSSetViewports(ID3D12GraphicsCommandList*, UINT, const D3D12_VIEWPORT*);
	void STDMETHODCALLTYPE Hook_RSSetScissorRects(ID3D12GraphicsCommandList*, UINT, const D3D12_RECT*);

	// Preserve descriptor heaps and root bindings inherited by injected passes.
	void STDMETHODCALLTYPE Hook_SetDescriptorHeaps(ID3D12GraphicsCommandList*, UINT, ID3D12DescriptorHeap* const*);
	void STDMETHODCALLTYPE Hook_SetComputeRootDescriptorTable(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Hook_SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Hook_SetComputeRoot32BitConstant(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	void STDMETHODCALLTYPE Hook_SetGraphicsRoot32BitConstant(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	void STDMETHODCALLTYPE Hook_SetComputeRoot32BitConstants(ID3D12GraphicsCommandList*, UINT, UINT, const void*, UINT);
	void STDMETHODCALLTYPE Hook_SetGraphicsRoot32BitConstants(ID3D12GraphicsCommandList*, UINT, UINT, const void*, UINT);
	void STDMETHODCALLTYPE Hook_SetComputeRootConstantBufferView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Hook_SetGraphicsRootConstantBufferView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Hook_SetComputeRootShaderResourceView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Hook_SetGraphicsRootShaderResourceView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Hook_SetComputeRootUnorderedAccessView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Hook_SetGraphicsRootUnorderedAccessView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);

	// Track geometry and render targets currently bound to the command list.
	void STDMETHODCALLTYPE Hook_IASetIndexBuffer(ID3D12GraphicsCommandList*, const D3D12_INDEX_BUFFER_VIEW*);
	void STDMETHODCALLTYPE Hook_IASetVertexBuffers(ID3D12GraphicsCommandList*, UINT, UINT, const D3D12_VERTEX_BUFFER_VIEW*);
	void STDMETHODCALLTYPE Hook_OMSetRenderTargets(ID3D12GraphicsCommandList*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, BOOL, const D3D12_CPU_DESCRIPTOR_HANDLE*);

	// Indirect execution is another boundary where a configured pass may run.
	void STDMETHODCALLTYPE Hook_ExecuteIndirect(ID3D12GraphicsCommandList*, ID3D12CommandSignature*, UINT, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64);

	// Capture active command-list state and schedule configured passes.
	void STDMETHODCALLTYPE Handle_DrawInstanced(ID3D12GraphicsCommandList*, UINT, UINT, UINT, UINT);
	void STDMETHODCALLTYPE Handle_DrawIndexedInstanced(ID3D12GraphicsCommandList*, UINT, UINT, UINT, INT, UINT);
	void STDMETHODCALLTYPE Handle_Dispatch(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	void STDMETHODCALLTYPE Handle_IASetPrimitiveTopology(ID3D12GraphicsCommandList*, D3D12_PRIMITIVE_TOPOLOGY);
	void STDMETHODCALLTYPE Handle_RSSetViewports(ID3D12GraphicsCommandList*, UINT, const D3D12_VIEWPORT*);
	void STDMETHODCALLTYPE Handle_RSSetScissorRects(ID3D12GraphicsCommandList*, UINT, const D3D12_RECT*);
	void STDMETHODCALLTYPE Handle_SetDescriptorHeaps(ID3D12GraphicsCommandList*, UINT, ID3D12DescriptorHeap* const*);
	void STDMETHODCALLTYPE Handle_SetComputeRootDescriptorTable(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Handle_SetGraphicsRootDescriptorTable(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE);
	void STDMETHODCALLTYPE Handle_SetComputeRoot32BitConstant(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	void STDMETHODCALLTYPE Handle_SetGraphicsRoot32BitConstant(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
	void STDMETHODCALLTYPE Handle_SetComputeRoot32BitConstants(ID3D12GraphicsCommandList*, UINT, UINT, const void*, UINT);
	void STDMETHODCALLTYPE Handle_SetGraphicsRoot32BitConstants(ID3D12GraphicsCommandList*, UINT, UINT, const void*, UINT);
	void STDMETHODCALLTYPE Handle_SetComputeRootConstantBufferView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Handle_SetGraphicsRootConstantBufferView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Handle_SetComputeRootShaderResourceView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Handle_SetGraphicsRootShaderResourceView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Handle_SetComputeRootUnorderedAccessView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Handle_SetGraphicsRootUnorderedAccessView(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);
	void STDMETHODCALLTYPE Handle_IASetIndexBuffer(ID3D12GraphicsCommandList*, const D3D12_INDEX_BUFFER_VIEW*);
	void STDMETHODCALLTYPE Handle_IASetVertexBuffers(ID3D12GraphicsCommandList*, UINT, UINT, const D3D12_VERTEX_BUFFER_VIEW*);
	void STDMETHODCALLTYPE Handle_OMSetRenderTargets(ID3D12GraphicsCommandList*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, BOOL, const D3D12_CPU_DESCRIPTOR_HANDLE*);
	void STDMETHODCALLTYPE Handle_ExecuteIndirect(ID3D12GraphicsCommandList*, ID3D12CommandSignature*, UINT, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64);

	//ID3D12PipelineLibrary
}
