#pragma once
#include <cstdint>
#include <dxgi.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <mutex>
#include <string>
#include <vector>

#include "ShaderReplacement.h"

namespace HookD3D12
{
	bool InstallD3D12CreateDeviceHook(HMODULE d3d12Module);
	void InstallPipelineHooksForDevice(ID3D12Device* device);
	void InstallCommandListHooksForCommandList(ID3D12GraphicsCommandList* commandList);

	enum class ShaderSelectionStyle
	{
		BluePixelShader = 0,
		Hidden = 1,
		None = 2,
	};

	struct D3D12PipelineInfo
	{
		std::string gpuName;

		UINT vendorId = 0;
		UINT deviceId = 0;

		SIZE_T dedicatedVideoMemory = 0;
		SIZE_T dedicatedSystemMemory = 0;
		SIZE_T sharedSystemMemory = 0;

		UINT resourceBindingTier = 0;
		UINT tiledResourcesTier = 0;
		UINT conservativeRasterTier = 0;
		UINT raytracingTier = 0;
		UINT meshShaderTier = 0;

		UINT swapChainBuffers = 0;
		DXGI_FORMAT swapChainFormat = DXGI_FORMAT_UNKNOWN;

		UINT commandQueueType = 0;
	};

	struct GraphicsPipelineInfo
	{
		ID3D12PipelineState* pipelineState = nullptr;

		uint64_t vsHash = 0; SIZE_T vsSize = 0;
		uint64_t psHash = 0; SIZE_T psSize = 0;
		uint64_t gsHash = 0; SIZE_T gsSize = 0;
		uint64_t hsHash = 0; SIZE_T hsSize = 0;
		uint64_t dsHash = 0; SIZE_T dsSize = 0;

		std::vector<uint8_t> vsBytecode;
		std::vector<uint8_t> psBytecode;
		std::vector<uint8_t> gsBytecode;
		std::vector<uint8_t> hsBytecode;
		std::vector<uint8_t> dsBytecode;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC originalDesc = {};

		bool psDisabled = false;
		ID3D12PipelineState* psoWithoutPS = nullptr;

		ID3D12PipelineState* psoWithReplacement = nullptr;
		std::string activeShaderReplacementName;
		ShaderReplacement::ShaderType activeShaderReplacementType = ShaderReplacement::Unknown;
		uint64_t activeShaderReplacementHash = 0;
		bool activeShaderReplacementUsesFallback = false;

		std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
		std::vector<std::string> inputElementSemanticNames;
		std::vector<D3D12_SO_DECLARATION_ENTRY> soDeclarations;
		std::vector<std::string> soSemanticNames;
		std::vector<UINT> soStrides;
	};

	struct PipelineStateInfo
	{
		ID3D12PipelineState* pipelineState = nullptr;

		uint64_t vsHash = 0; SIZE_T vsSize = 0;
		uint64_t psHash = 0; SIZE_T psSize = 0;
		uint64_t gsHash = 0; SIZE_T gsSize = 0;
		uint64_t hsHash = 0; SIZE_T hsSize = 0;
		uint64_t dsHash = 0; SIZE_T dsSize = 0;
		uint64_t csHash = 0; SIZE_T csSize = 0;

		bool isGraphics = false;
		bool isCompute = false;
		ID3D12RootSignature* rootSignature = nullptr;

		std::vector<uint8_t> streamBlob;

		bool vsDisabled = false;  ID3D12PipelineState* psoWithoutVS = nullptr;
		bool psDisabled = false;  ID3D12PipelineState* psoWithoutPS = nullptr;
		bool csDisabled = false;  ID3D12PipelineState* psoWithoutCS = nullptr;
		bool gsDisabled = false;  ID3D12PipelineState* psoWithoutGS = nullptr;
		bool hsDisabled = false;  ID3D12PipelineState* psoWithoutHS = nullptr;
		bool dsDisabled = false;  ID3D12PipelineState* psoWithoutDS = nullptr;

		ID3D12PipelineState* psoWithReplacement = nullptr;
		std::string activeShaderReplacementName;
		ShaderReplacement::ShaderType activeShaderReplacementType = ShaderReplacement::Unknown;
		uint64_t activeShaderReplacementHash = 0;
		bool activeShaderReplacementUsesFallback = false;

		std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
		std::vector<std::string> inputElementSemanticNames;
		std::vector<D3D12_SO_DECLARATION_ENTRY> soDeclarations;
		std::vector<std::string> soSemanticNames;
		std::vector<UINT> soStrides;

		std::vector<uint8_t> vsBytecode;
		std::vector<uint8_t> psBytecode;
		std::vector<uint8_t> gsBytecode;
		std::vector<uint8_t> hsBytecode;
		std::vector<uint8_t> dsBytecode;
		std::vector<uint8_t> csBytecode;
	};

	struct UncapturedPipelineStateInfo
	{
		ID3D12PipelineState* pipelineState = nullptr;
		uint64_t cachedBlobHash = 0;
		SIZE_T cachedBlobSize = 0;
		std::vector<uint8_t> cachedBlob;
		bool attemptedReplacement = false;
		ID3D12PipelineState* replacementPipelineState = nullptr;
		ID3D12RootSignature* observedGraphicsRootSignature = nullptr;
		ID3D12RootSignature* observedComputeRootSignature = nullptr;
		std::string activeShaderReplacementName;
		ShaderReplacement::ShaderType activeShaderReplacementType = ShaderReplacement::Unknown;
		uint64_t activeShaderReplacementHash = 0;
	};

	struct FrameContext
	{
		ID3D12CommandAllocator* allocator;
		ID3D12Resource* renderTarget;
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
	};

	struct ComputePipelineInfo
	{
		ID3D12PipelineState* pipelineState = nullptr;

		uint64_t csHash = 0;
		SIZE_T csSize = 0;
	};

	struct RootSignatureInfo
	{
		uint64_t hash = 0;
		std::vector<uint8_t> blob;
	};

	struct PSOPendingRebuild
	{
		enum class SourceList { Graphics, Stream };
		SourceList source;
		int index;
		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE targetType;
	};

	extern std::vector<PSOPendingRebuild> gPendingRebuilds;
	extern std::mutex gPipelineMutex;
	extern std::vector<GraphicsPipelineInfo> gGraphicsPipelines;
	extern D3D12PipelineInfo gPipelineInfo;
	extern std::vector<PipelineStateInfo> gPipelineStates;
	extern std::vector<ShaderReplacement::ShaderReplacementDisk> gLoadedShaderReplacements;
	extern std::vector<std::vector<uint8_t>> gLoadedShaderReplacementBlobs;
	extern int gSelectedShaderReplacementIndex;
	extern int gShaderReplacementNameBufferIndex;
	extern char gShaderReplacementNameBuffer[256];
	extern bool gLoadedShaderReplacementsOnce;
	extern ShaderSelectionStyle gShaderSelectionStyle;

	int FindEnabledShaderReplacement(uint64_t shaderHash, ShaderReplacement::ShaderType shaderType);
	void MarkShaderReplacementApplyDirty();
	void InvalidateAllReplacementPSOs();
	void ResetUncapturedReplacementAttempts();
	void ClearShaderMarkers();
	void InvalidateShaderMarkerPSOs();
	void CaptureGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, ID3D12PipelineState* pipelineState);
	void CaptureComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, ID3D12PipelineState* pipelineState, bool registerKnownPipeline);
	void CapturePipelineStateStream(const D3D12_PIPELINE_STATE_STREAM_DESC* desc, ID3D12PipelineState* pipelineState);
	bool GetRootSignatureBlob(ID3D12RootSignature* rootSignature, std::vector<uint8_t>& outBlob, uint64_t& outHash);
	ID3D12RootSignature* GetOrCreatePersistedRootSignature(const ShaderReplacement::ShaderReplacementDisk& replacement, ID3D12Device* device);
	void ReleaseRootSignatureCache();
	void RefreshLoadedShaderReplacements();
	void SyncShaderReplacementNameBuffer();
	bool SaveShaderReplacement(int index);
	bool CompileShaderReplacement(int index);
	bool ReloadShaderReplacement(int index);
	bool DeleteShaderReplacement(int index);
	bool CreateReplacementShaderTemplateForPipeline(
		const std::string& sourceList,
		int pipelineIndex,
		ShaderReplacement::ShaderType shaderType,
		uint64_t shaderHash,
		size_t shaderBytecodeLength,
		const void* shaderBytecode,
		GraphicsPipelineInfo& pipeline);
	bool CreateReplacementShaderTemplateForPipeline(
		const std::string& sourceList,
		int pipelineIndex,
		ShaderReplacement::ShaderType shaderType,
		uint64_t shaderHash,
		size_t shaderBytecodeLength,
		const void* shaderBytecode,
		PipelineStateInfo& pipeline);


	extern void Release();
	bool IsInitialized();

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE DEVICE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE DEVICE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE DEVICE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	typedef HRESULT(WINAPI* FunctionCreateDeviceD3D12)(IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid, void** ppDevice);

	extern FunctionCreateDeviceD3D12 Original_CreateDeviceD3D12;

	HRESULT WINAPI Hook_CreateDeviceD3D12(IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid, void** ppDevice);

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| PRESENT |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| PRESENT |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| PRESENT |||||||||||||||||||||||||||||||||||||||||||||||||||||

	typedef HRESULT(STDMETHODCALLTYPE* FunctionPresentD3D12)(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags);
	typedef HRESULT(STDMETHODCALLTYPE* FunctionPresent1D3D12)(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pParams);

	extern FunctionPresentD3D12 Original_PresentD3D12;
	extern FunctionPresent1D3D12 Original_Present1D3D12;

	extern HRESULT STDMETHODCALLTYPE Hook_PresentD3D12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags);
	extern HRESULT STDMETHODCALLTYPE Hook_Present1D3D12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pParams);

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| EXECUTE COMMAND LISTS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| EXECUTE COMMAND LISTS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| EXECUTE COMMAND LISTS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	typedef void(STDMETHODCALLTYPE* FunctionExecuteCommandListsD3D12)(ID3D12CommandQueue* _this, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists);

	extern FunctionExecuteCommandListsD3D12 Original_ExecuteCommandListsD3D12;

	extern void STDMETHODCALLTYPE Hook_ExecuteCommandListsD3D12(ID3D12CommandQueue* _this, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists);

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| RESIZE BUFFERS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| RESIZE BUFFERS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| RESIZE BUFFERS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	typedef HRESULT(STDMETHODCALLTYPE* FunctionResizeBuffersD3D12)(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);

	extern FunctionResizeBuffersD3D12 Original_ResizeBuffersD3D12;

	extern HRESULT STDMETHODCALLTYPE Hook_ResizeBuffersD3D12(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE PIPELINE LIBRARY |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE PIPELINE LIBRARY |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE PIPELINE LIBRARY |||||||||||||||||||||||||||||||||||||||||||||||||||||

	typedef HRESULT(__stdcall* FunctionCreatePipelineLibraryD3D12)(ID3D12Device1*, const void*, SIZE_T, REFIID, void**);

	extern FunctionCreatePipelineLibraryD3D12 Original_CreatePipelineLibrary;

	extern HRESULT __stdcall Hook_CreatePipelineLibrary(ID3D12Device1* device, const void* pLibraryBlob, SIZE_T blobLength, REFIID riid, void** ppPipelineLibrary);

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE GRAPHICS PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE GRAPHICS PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE GRAPHICS PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	using FunctionCreateGraphicsPipelineStateD3D12 = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, REFIID, void**);

	extern FunctionCreateGraphicsPipelineStateD3D12 Original_CreateGraphicsPipelineState;

	//IMPORTANT NOTE: while this works and I will keep it around, it looks like for the most part this actually catches the imgui pixel shaders
	//so we can actually disable some of the pixel shaders of the imgui rendering. 
	//not really what I want but going to keep around because it's good to have just in case, but most of the game rendering is actually through CreatePipelineState
	extern HRESULT STDMETHODCALLTYPE Hook_CreateGraphicsPipelineState(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID riid, void** ppPipelineState);

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| LOAD GRAPHICS PIPELINE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| LOAD GRAPHICS PIPELINE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| LOAD GRAPHICS PIPELINE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	typedef HRESULT(__stdcall* FunctionLoadGraphicsPipelineD3D12)(ID3D12PipelineLibrary*, LPCWSTR, const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, REFIID, void**);

	extern FunctionLoadGraphicsPipelineD3D12 Original_LoadGraphicsPipeline;

	HRESULT __stdcall Hook_LoadGraphicsPipeline(ID3D12PipelineLibrary* library, LPCWSTR name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID riid, void** ppPipelineState);

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE COMPUTE PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE COMPUTE PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE COMPUTE PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	using FunctionCreateComputePipelineStateD3D12 = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_COMPUTE_PIPELINE_STATE_DESC*, REFIID, void**);

	extern FunctionCreateComputePipelineStateD3D12 Original_CreateComputePipelineState;

	extern HRESULT STDMETHODCALLTYPE Hook_CreateComputePipelineState(ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, REFIID riid, void** ppPipelineState);

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	using FunctionCreateRootSignatureD3D12 = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, UINT, const void*, SIZE_T, REFIID, void**);

	extern FunctionCreateRootSignatureD3D12 Original_CreateRootSignature;

	extern HRESULT STDMETHODCALLTYPE Hook_CreateRootSignature(ID3D12Device* device, UINT nodeMask, const void* blobWithRootSignature, SIZE_T blobLengthInBytes, REFIID riid, void** rootSignature);

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| LOAD COMPUTE PIPELINE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| LOAD COMPUTE PIPELINE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| LOAD COMPUTE PIPELINE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	typedef HRESULT(__stdcall* FunctionLoadComputePipelineD3D12)(ID3D12PipelineLibrary*, LPCWSTR, const D3D12_COMPUTE_PIPELINE_STATE_DESC*, REFIID, void**);

	extern FunctionLoadComputePipelineD3D12 Original_LoadComputePipeline;

	extern HRESULT __stdcall Hook_LoadComputePipeline(ID3D12PipelineLibrary* library, LPCWSTR name, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, REFIID riid, void** ppPipelineState);

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| LOAD PIPELINE STREAM |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| LOAD PIPELINE STREAM |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| LOAD PIPELINE STREAM |||||||||||||||||||||||||||||||||||||||||||||||||||||

	typedef HRESULT(__stdcall* FunctionLoadPipelineD3D12)(ID3D12PipelineLibrary1*, LPCWSTR, const D3D12_PIPELINE_STATE_STREAM_DESC*, REFIID, void**);

	extern FunctionLoadPipelineD3D12 Original_LoadPipeline;

	extern HRESULT __stdcall Hook_LoadPipeline(ID3D12PipelineLibrary1* library, LPCWSTR name, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, REFIID riid, void** ppPipelineState);

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	using FunctionCreatePipelineStateD3D12 = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device2*, const D3D12_PIPELINE_STATE_STREAM_DESC*, REFIID, void**);

	extern FunctionCreatePipelineStateD3D12 Original_CreatePipelineState;

	//IMPORTANT NOTE: the majority of the game has these calls for the shader resources.
	//checking via renderdoc it seems all shader resources are ID3D12Device2::CreatePipelineState
	//so this is where some of the real magic actually is
	extern HRESULT STDMETHODCALLTYPE Hook_CreatePipelineState(ID3D12Device2* device, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, REFIID riid, void** ppPipelineState);

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SET PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SET PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SET PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	using FunctionSetPipelineStateD3D12 = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);

	extern FunctionSetPipelineStateD3D12 Original_SetPipelineState;

	extern void STDMETHODCALLTYPE Hook_SetPipelineState(ID3D12GraphicsCommandList* cmdList, ID3D12PipelineState* pso);

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SET COMPUTE ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SET COMPUTE ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SET COMPUTE ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	using FunctionSetComputeRootSignatureD3D12 = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12RootSignature*);

	extern FunctionSetComputeRootSignatureD3D12 Original_SetComputeRootSignature;

	extern void STDMETHODCALLTYPE Hook_SetComputeRootSignature(ID3D12GraphicsCommandList* cmdList, ID3D12RootSignature* rootSignature);

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SET GRAPHICS ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SET GRAPHICS ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SET GRAPHICS ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	using FunctionSetGraphicsRootSignatureD3D12 = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12RootSignature*);

	extern FunctionSetGraphicsRootSignatureD3D12 Original_SetGraphicsRootSignature;

	extern void STDMETHODCALLTYPE Hook_SetGraphicsRootSignature(ID3D12GraphicsCommandList* cmdList, ID3D12RootSignature* rootSignature);

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SET PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SET PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SET PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	using FunctionResetGraphicsCommandListD3D12 = HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);

	extern FunctionResetGraphicsCommandListD3D12 Original_ResetGraphicsCommandList;

	extern HRESULT STDMETHODCALLTYPE Hook_ResetGraphicsCommandList(ID3D12GraphicsCommandList* cmdList, ID3D12CommandAllocator* allocator, ID3D12PipelineState* initialState);
	
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| STORE PIPELINE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| STORE PIPELINE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| STORE PIPELINE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	typedef HRESULT(__stdcall* FunctionStorePipelineD3D12)(ID3D12PipelineLibrary*, LPCWSTR, ID3D12PipelineState*);

	extern FunctionStorePipelineD3D12 Original_StorePipeline;

	HRESULT __stdcall Hook_StorePipeline(ID3D12PipelineLibrary* library, LPCWSTR name, ID3D12PipelineState* pso);

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| GET SERIALIZED SIZE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| GET SERIALIZED SIZE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| GET SERIALIZED SIZE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	typedef SIZE_T(__stdcall* FunctionGetSerializedSizeD3D12)(ID3D12PipelineLibrary*);

	extern FunctionGetSerializedSizeD3D12 Original_GetSerializedSize;

	extern SIZE_T __stdcall Hook_GetSerializedSize(ID3D12PipelineLibrary* library);

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SERIALIZE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SERIALIZE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SERIALIZE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	typedef HRESULT(__stdcall* FunctionSerializeD3D12)(ID3D12PipelineLibrary*, void*, SIZE_T);

	extern FunctionSerializeD3D12 Original_Serialize;

	extern HRESULT __stdcall Hook_Serialize(ID3D12PipelineLibrary* library, void* data, SIZE_T dataSize);
}
