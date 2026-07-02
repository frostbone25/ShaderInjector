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
#include "DatabaseShaderTargets.h"
#include "DatabaseGraphicsPSOs.h"
#include "DatabaseStreamPSOs.h"
#include "HookInput.h"
#include "ShaderInjectorIO.h"
#include "ShaderTarget.h"
#include "ShaderInjectorGUI.h"
#include "Hash.h"
#include "FPSCounter.h"
#include "ShaderAutomaticDiscovery.h"
#include "HookD3D12PipelineUtils.h"
#include "HookD3D12ReplacementLookup.h"
#include "HookD3D12ReplacementTemplates.h"
#include "HookD3D12OverlayStartup.h"
#include "HookD3D12PipelineRegistry.h"
#include "VTableIndex.h"
#include "StringHelper.h"

#if defined _M_X64
typedef uint64_t uintx_t;
#elif defined _M_IX86
typedef uint32_t uintx_t;
#endif

namespace HookD3D12
{
	static ID3D12Device*              gDevice = nullptr;
	static ID3D12Device*              gDevice2 = nullptr;
	static ID3D12CommandQueue*        gCommandQueue = nullptr;
	static ID3D12DescriptorHeap*      gHeapRTV = nullptr;
	static ID3D12DescriptorHeap*      gHeapSRV = nullptr;
	static ID3D12GraphicsCommandList* gCommandList = nullptr;
	static ID3D12Fence*               gOverlayFence = nullptr;
	static HANDLE                     gFenceEvent = nullptr;
	static UINT64                     gOverlayFenceValue = 0;
	static uintx_t                    gBufferCount = 0;

	static FrameContext* gFrameContexts = nullptr;
	static bool          gInitialized = false;
	static bool          gShutdown = false;
	static ULONGLONG     gOverlayInitializedTick = 0;
	static bool          gLoggedStartupMenuDelay = false;
	static bool          gOverlayRenderingDisabled = false;
	static bool          gAfterFirstPresent = false;
	static bool          gLoggedPresentHook = false;
	static bool          gLoggedPresent1Hook = false;
	static bool          gLoggedCommandQueueCaptured = false;
	static bool          gLoggedOverlayInitialized = false;

	static std::vector<UncapturedPipelineStateInfo> gUncapturedPipelineStates;
	static std::unordered_map<ID3D12GraphicsCommandList*, ID3D12RootSignature*> gCurrentGraphicsRootSignatureByCommandList;
	static std::unordered_map<ID3D12GraphicsCommandList*, ID3D12RootSignature*> gCurrentComputeRootSignatureByCommandList;

	std::vector<PSOPendingRebuild> gPendingRebuilds;

	std::mutex gPipelineMutex;
	D3D12PipelineInfo gPipelineInfo;

	static std::unordered_map<ID3D12PipelineState*, ID3D12PipelineState*> gPipelineStateOverrides;
	static std::vector<ID3D12PipelineState*> gRetiredPipelineStates;
	static std::unordered_set<ID3D12PipelineState*> gRetiredPipelineStateSet;

	static bool gShaderTargetApplyDirty = true;
	static bool gPipelineStateOverridesDirty = true;
	ShaderSelectionStyle gShaderSelectionStyle = ShaderSelectionStyle::BluePixelShader;

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE DEVICE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE DEVICE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE DEVICE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	void MarkShaderTargetApplyDirty()
	{
		gShaderTargetApplyDirty = true;
		gPipelineStateOverridesDirty = true;
	}

	void ResetUncapturedReplacementAttempts()
	{
		for (auto& uncaptured : gUncapturedPipelineStates)
			uncaptured.attemptedReplacement = false;
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
	}

	void ClearReplacementPSO(PipelineStateInfo& pipeline)
	{
		RetirePipelineState(pipeline.psoWithReplacement);

		pipeline.activeShaderTargetName.clear();
		pipeline.activeShaderTargetType = ShaderTarget::Unknown;
		pipeline.activeShaderTargetHash = 0;
		pipeline.activeShaderTargetUsesFallback = false;
	}

	void InvalidateAllReplacementPSOs()
	{
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
			if (uncaptured.replacementPipelineState && trackedReplacementPSOs.find(uncaptured.replacementPipelineState) == trackedReplacementPSOs.end())
				RetirePipelineState(uncaptured.replacementPipelineState);
			else
				uncaptured.replacementPipelineState = nullptr;
			uncaptured.activeShaderTargetName.clear();
			uncaptured.activeShaderTargetType = ShaderTarget::Unknown;
			uncaptured.activeShaderTargetHash = 0;
			uncaptured.attemptedReplacement = false;
		}

		gPipelineStateOverrides.clear();
		gPipelineStateOverridesDirty = true;
	}

	void RebuildPipelineStateOverrideMap()
	{
		gPipelineStateOverrides.clear();

		for (auto& pipeline : gGraphicsPipelines)
		{
			if (!pipeline.pipelineState)
				continue;

			if (pipeline.psDisabled && pipeline.psoWithoutPS)
			{
				gPipelineStateOverrides[pipeline.pipelineState] = pipeline.psoWithoutPS;
				continue;
			}

			if (pipeline.psoWithReplacement && ReplacementStillEnabled(pipeline.activeShaderTargetName, pipeline.activeShaderTargetHash, pipeline.activeShaderTargetType))
				gPipelineStateOverrides[pipeline.pipelineState] = pipeline.psoWithReplacement;
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

			if (pipeline.psoWithReplacement && ReplacementStillEnabled(pipeline.activeShaderTargetName, pipeline.activeShaderTargetHash, pipeline.activeShaderTargetType))
				gPipelineStateOverrides[pipeline.pipelineState] = pipeline.psoWithReplacement;
		}

		for (auto& uncaptured : gUncapturedPipelineStates)
		{
			if (!uncaptured.pipelineState || !uncaptured.replacementPipelineState)
				continue;

			if (ReplacementStillEnabled(uncaptured.activeShaderTargetName, uncaptured.activeShaderTargetHash, uncaptured.activeShaderTargetType))
				gPipelineStateOverrides[uncaptured.pipelineState] = uncaptured.replacementPipelineState;
		}

		gPipelineStateOverridesDirty = false;
	}


	void GatherPipelineInfo(IDXGISwapChain3* swapChain)
	{
		GatherD3D12PipelineInfo(swapChain, gDevice, gCommandQueue, gPipelineInfo);
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| HOOK - SET GRAPHICS ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| HOOK - SET GRAPHICS ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| HOOK - SET GRAPHICS ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	void STDMETHODCALLTYPE Hook_SetGraphicsRootSignature(ID3D12GraphicsCommandList* cmdList, ID3D12RootSignature* rootSignature)
	{	
		//IMPORTANT NOTE: brackets are important here, we need to limit the scope when collecting the root signature
		{
			std::lock_guard<std::mutex> lock(gPipelineMutex);
			gCurrentGraphicsRootSignatureByCommandList[cmdList] = rootSignature;
		}

		Original_SetGraphicsRootSignature(cmdList, rootSignature);
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| HOOK - SET COMPUTE ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| HOOK - SET COMPUTE ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| HOOK - SET COMPUTE ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	void STDMETHODCALLTYPE Hook_SetComputeRootSignature(ID3D12GraphicsCommandList* cmdList, ID3D12RootSignature* rootSignature)
	{
		//IMPORTANT NOTE: brackets are important here, we need to limit the scope when collecting the root signature
		{
			std::lock_guard<std::mutex> lock(gPipelineMutex);
			gCurrentComputeRootSignatureByCommandList[cmdList] = rootSignature;
		}

		Original_SetComputeRootSignature(cmdList, rootSignature);
	}

	void RecordUncapturedPipelineStateLocked(ID3D12GraphicsCommandList* cmdList, ID3D12PipelineState* pipelineState, const char* reason)
	{
		if (!pipelineState)
			return;

		auto graphicsRootIt = gCurrentGraphicsRootSignatureByCommandList.find(cmdList);
		ID3D12RootSignature* observedGraphicsRootSignature = graphicsRootIt != gCurrentGraphicsRootSignatureByCommandList.end() ? graphicsRootIt->second : nullptr;
		auto computeRootIt = gCurrentComputeRootSignatureByCommandList.find(cmdList);
		ID3D12RootSignature* observedComputeRootSignature = computeRootIt != gCurrentComputeRootSignatureByCommandList.end() ? computeRootIt->second : nullptr;

		for (auto& info : gUncapturedPipelineStates)
		{
			if (info.pipelineState == pipelineState)
			{
				bool updated = false;

				if (!info.observedGraphicsRootSignature && observedGraphicsRootSignature)
				{
					info.observedGraphicsRootSignature = observedGraphicsRootSignature;
					updated = true;
				}

				if (!info.observedComputeRootSignature && observedComputeRootSignature)
				{
					info.observedComputeRootSignature = observedComputeRootSignature;
					updated = true;
				}

				if (updated)
				{
					info.attemptedReplacement = false;
					MarkShaderTargetApplyDirty();
				}

				return;
			}
		}

		UncapturedPipelineStateInfo info{};
		info.pipelineState = pipelineState;
		info.observedGraphicsRootSignature = observedGraphicsRootSignature;
		info.observedComputeRootSignature = observedComputeRootSignature;
		GetPipelineCachedBlobInfo(pipelineState, info.cachedBlobHash, info.cachedBlobSize, &info.cachedBlob);

		if (!info.cachedBlob.empty())
		{
			std::string pointerText = StringHelper::PointerToString(pipelineState);
			std::replace(pointerText.begin(), pointerText.end(), ':', '_');
			std::string path = ShaderInjectorIO::JoinPath(ShaderInjectorIO::GetUncapturedPSODirectory(), "UncapturedPSO_" + Hash::FormatHash(info.cachedBlobHash) + "_" + pointerText + ShaderInjectorIO::extensionBIN);
			ShaderInjectorIO::WriteBinaryFile(path, info.cachedBlob.data(), info.cachedBlob.size());
		}

		gUncapturedPipelineStates.push_back(info);

		//char buffer[384];
		//sprintf_s(buffer, "HookD3D12->RecordUncapturedPipelineStateLocked: %s uncaptured PSO=%p cachedHash=%s cachedBytes=%zu", reason ? reason : "Bound", pipelineState, info.cachedBlobHash ? Hash::FormatHash(info.cachedBlobHash).c_str() : "<none>", (size_t)info.cachedBlobSize);
		//ShaderInjectorGUI::WriteToRuntimeLog(buffer);

		if (info.cachedBlobHash)
			MarkShaderTargetApplyDirty();
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SET PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SET PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SET PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	void STDMETHODCALLTYPE Hook_SetPipelineState(ID3D12GraphicsCommandList* cmdList, ID3D12PipelineState* pso)
	{
		if (!Globals::gShaderInjectorEnabled)
		{
			Original_SetPipelineState(cmdList, pso);
			return;
		}

		//IMPORTANT NOTE: brackets are important here, we need to limit the scope when collecting the root signature
		{
			std::lock_guard<std::mutex> lock(gPipelineMutex);

			if (gPipelineStateOverridesDirty)
				RebuildPipelineStateOverrideMap();

			if (!IsKnownPipelineStateLocked(pso) && MarkUntrackedBoundPipelineStateLocked(pso))
				RecordUncapturedPipelineStateLocked(cmdList, pso, "SetPipelineState bound");

			auto overrideIt = gPipelineStateOverrides.find(pso);

			if (overrideIt != gPipelineStateOverrides.end() && overrideIt->second)
			{
				Original_SetPipelineState(cmdList, overrideIt->second);
				return;
			}
		}

		Original_SetPipelineState(cmdList, pso);
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| RESET GRAPHICS COMMAND LIST |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| RESET GRAPHICS COMMAND LIST |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| RESET GRAPHICS COMMAND LIST |||||||||||||||||||||||||||||||||||||||||||||||||||||

	HRESULT STDMETHODCALLTYPE Hook_ResetGraphicsCommandList(ID3D12GraphicsCommandList* cmdList, ID3D12CommandAllocator* allocator, ID3D12PipelineState* initialState)
	{
		if (!Globals::gShaderInjectorEnabled)
			return Original_ResetGraphicsCommandList(cmdList, allocator, initialState);

		ID3D12PipelineState* boundState = initialState;

		//IMPORTANT NOTE: brackets are important here, we need to limit the scope when collecting the root signature
		{
			std::lock_guard<std::mutex> lock(gPipelineMutex);

			if (gPipelineStateOverridesDirty)
				RebuildPipelineStateOverrideMap();

			if (!IsKnownPipelineStateLocked(initialState) && MarkUntrackedBoundPipelineStateLocked(initialState))
				RecordUncapturedPipelineStateLocked(cmdList, initialState, "CommandList Reset bound initial");

			auto overrideIt = gPipelineStateOverrides.find(initialState);

			if (overrideIt != gPipelineStateOverrides.end() && overrideIt->second)
				boundState = overrideIt->second;
		}

		return Original_ResetGraphicsCommandList(cmdList, allocator, boundState);
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE COMPUTE PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE COMPUTE PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE COMPUTE PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	HRESULT STDMETHODCALLTYPE Hook_CreateComputePipelineState(ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, REFIID riid, void** ppPipelineState)
	{
		static bool shown = false;

		if (!shown)
		{
			shown = true;
			//MessageBoxA(nullptr, "CreateComputePipelineState Hook Hit", "Shader Injector", MB_OK);
		}

		//IMPORTANT NOTE: this does not get executed

		HRESULT hr = Original_CreateComputePipelineState(device, desc, riid, ppPipelineState);

		if (SUCCEEDED(hr) && desc && ppPipelineState && *ppPipelineState)
			CaptureComputePipelineState(desc, (ID3D12PipelineState*)*ppPipelineState, true);

		return hr;
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE GRAPHICS PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE GRAPHICS PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE GRAPHICS PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//IMPORTANT NOTE: while this works and I will keep it around, it looks like for the most part this actually catches the imgui pixel shaders
	//so we can actually disable some of the pixel shaders of the imgui rendering. 
	//not really what I want but going to keep around because it's good to have just in case, but most of the game rendering is actually through CreatePipelineState
	HRESULT STDMETHODCALLTYPE Hook_CreateGraphicsPipelineState(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID riid, void** ppPipelineState)
	{
		//IMPORTANT NOTE: this does get executed!

		HRESULT hr = Original_CreateGraphicsPipelineState(device, desc, riid, ppPipelineState);

		if (SUCCEEDED(hr) && desc && ppPipelineState && *ppPipelineState)
			CaptureGraphicsPipelineState(desc, (ID3D12PipelineState*)*ppPipelineState);

		return hr;
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE PIPELINE STATE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//IMPORTANT NOTE: the majority of the game has these calls for the shader resources.
	//checking via renderdoc it seems all shader resources are ID3D12Device2::CreatePipelineState
	//so this is where some of the real magic actually is
	HRESULT STDMETHODCALLTYPE Hook_CreatePipelineState(ID3D12Device2* device, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, REFIID riid, void** ppPipelineState)
	{
		HRESULT hr = Original_CreatePipelineState(device, desc, riid, ppPipelineState);

		//NOTE: haven't hit any of these yet, seems to pass
		if (FAILED(hr))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->Hook_CreatePipelineState: not succeded.");
			return hr;
		}

		//NOTE: haven't hit any of these yet, seems to pass
		if (!desc || !ppPipelineState || !*ppPipelineState)
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->Hook_CreatePipelineState: not succeded, no objects");
			return hr;
		}

		CapturePipelineStateStream(desc, (ID3D12PipelineState*)*ppPipelineState);

		return hr;
	}

	HRESULT CreatePipelineStateInternal(ID3D12Device2* device, const D3D12_PIPELINE_STATE_STREAM_DESC* desc, REFIID riid, void** ppPSO)
	{
		return Original_CreatePipelineState(device, desc, riid, ppPSO);
	}

	void RebuildStreamPSOWithoutStage(
		PipelineStateInfo& p,
		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE targetType,
		ID3D12PipelineState*& outPSO,
		ID3D12Device* device)  // <-- ID3D12Device* not ID3D12Device2*
	{
		if (outPSO || p.streamBlob.empty() || !device)
			return;

		const bool hiddenSelection = gShaderSelectionStyle == ShaderSelectionStyle::Hidden;

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

			if (type == targetType)
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

				if (!hiddenSelection && targetType == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS)
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

				patchedTarget = true;
			}

			// Always zero out CachedPSO regardless of target —
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
				// so null it out — most PSOs don't need it for non-VS stages anyway,
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
		HRESULT hr = Original_CreateGraphicsPipelineState(gDevice, &desc, IID_PPV_ARGS(&rebuiltPipelineState));

		if (FAILED(hr) || !rebuiltPipelineState)
		{
			if (rebuiltPipelineState)
				rebuiltPipelineState->Release();

			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildGraphicsPSOWithReplacement: failed hr=" + std::to_string((unsigned)hr) + " replacement=" + replacement.name);
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
		ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->RebuildGraphicsPSOWithReplacement: Applied graphics shader replacement: " + replacement.name + (usedFallback ? " (null shader fallback)" : ""));
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

			ptr += subobjectSize;
		}

		if (missingRootSignature)
		{
			device2->Release();
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildStreamPSOWithReplacement: persisted stream template needs an observed root signature for " + replacement.name);
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
		HRESULT hr = CreatePipelineStateInternal(device2, &patchedDesc, IID_PPV_ARGS(&rebuiltPipelineState));

		device2->Release();

		if (FAILED(hr) || !rebuiltPipelineState)
		{
			if (rebuiltPipelineState)
				rebuiltPipelineState->Release();

			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->RebuildStreamPSOWithReplacement: failed hr=" + std::to_string((unsigned)hr) + " replacement=" + replacement.name + " streamBytes=" + std::to_string(patchedBlob.size()) + " root=" + StringHelper::PointerToString(rootSignatureOverride) + " vsBytes=" + std::to_string(pipeline.vsBytecode.size()) + " psBytes=" + std::to_string(pipeline.psBytecode.size()) + " inputElements=" + std::to_string(pipeline.inputElements.size()));
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
		ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->RebuildStreamPSOWithReplacement: Applied stream shader replacement: " + replacement.name + (usedFallback ? " (null shader fallback)" : ""));
		return true;
	}

	void TryApplyGraphicsReplacement(GraphicsPipelineInfo& pipeline)
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
				RebuildGraphicsPSOWithReplacement(pipeline, replacementIndex, candidate.hash, candidate.type);
				return;
			}
		}
	}

	void TryApplyStreamReplacement(PipelineStateInfo& pipeline)
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
				RebuildStreamPSOWithReplacement(pipeline, replacementIndex, candidate.hash, candidate.type);
				return;
			}
		}
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

		if (TryRebuildWithRootSignature(observedRootSignature, "observed command-list root signature"))
			return true;

		if (persistedRootSignature && persistedRootSignature != observedRootSignature)
		{
			if (observedRootSignature)
				ShaderInjectorGUI::WriteToRuntimeLogWarning("HookD3D12->TryApplyPersistedStreamTemplateToUncaptured: Observed root signature rebuild failed; retrying persisted root signature blob: " + replacement.name + templateLogSuffix);

			if (TryRebuildWithRootSignature(persistedRootSignature, "persisted root signature blob"))
				return true;
		}

		if (!attemptedAnyRootSignature)
			ShaderInjectorGUI::WriteToRuntimeLogWarning("HookD3D12->TryApplyPersistedStreamTemplateToUncaptured: Uncaptured PSO matched persisted stream template, but no root signature is available: " + replacement.name + templateLogSuffix);

		return false;
	}

	bool TryApplyUncapturedReplacement(UncapturedPipelineStateInfo& uncaptured)
	{
		if (!uncaptured.pipelineState || !uncaptured.cachedBlobHash)
			return false;

		int replacementIndex = FindEnabledShaderTargetByCachedBlob(uncaptured.cachedBlobHash);
		const char* matchMethod = "cached blob hash";

		if (replacementIndex < 0 && !uncaptured.cachedBlob.empty())
		{
			double matchingRatio = 0.0;
			size_t longestMatchingRun = 0;
			replacementIndex = FindEnabledShaderTargetByCachedBlobContent(uncaptured.cachedBlob, matchingRatio, longestMatchingRun);

			if (replacementIndex >= 0)
			{
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

			static int noMatchLogCount = 0;
			if (noMatchLogCount < 40)
			{
				int enabledCount = 0;
				int cachedHashCount = 0;
				for (const auto& replacement : gLoadedShaderTargets)
				{
					if (!replacement.enabled)
						continue;

					enabledCount++;

					if (!replacement.pipelineCachedBlobHash.empty())
						cachedHashCount++;
				}

				ShaderInjectorGUI::WriteToRuntimeLog(
					"HookD3D12->TryApplyUncapturedReplacement: Uncaptured PSO has no replacement match: cachedHash=" + Hash::FormatHash(uncaptured.cachedBlobHash) +
					" cachedBytes=" + std::to_string((size_t)uncaptured.cachedBlobSize) +
					" enabledReplacements=" + std::to_string(enabledCount) +
					" replacementsWithCachedHash=" + std::to_string(cachedHashCount));
				noMatchLogCount++;
			}

			return false;
		}

		ShaderTarget::ShaderTargetDisk& replacement = gLoadedShaderTargets[replacementIndex];
		const uint64_t shaderHash = Hash::ParseHashText(replacement.originalShaderBytecodeHash);

		if (!shaderHash || replacement.shaderType == ShaderTarget::Unknown)
		{
			uncaptured.attemptedReplacement = true;
			return false;
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

		if ((replacement.sourceList == "Stream" || !replacement.pipelineStreamBlobPath.empty()) &&
			TryApplyPersistedStreamTemplateToUncaptured(uncaptured, replacementIndex, shaderHash, replacement.shaderType, matchMethod))
		{
			return true;
		}

		uncaptured.attemptedReplacement = true;
		ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->TryApplyUncapturedReplacement: Uncaptured PSO matched replacement cached blob, but no rebuild template is currently available: " + replacement.name);
		return false;
	}

	void ApplyShaderTargetPSOs()
	{
		ShaderAutomaticDiscovery::ProcessQueuedWork(1);

		if (!Globals::gShaderInjectorEnabled)
			return;

		if (!gShaderTargetApplyDirty)
			return;

		if (!gLoadedShaderTargetsOnce)
			RefreshLoadedShaderTargets();

		std::lock_guard<std::mutex> lock(gPipelineMutex);

		for (auto& pipeline : gGraphicsPipelines)
			TryApplyGraphicsReplacement(pipeline);

		for (auto& pipeline : gPipelineStates)
			TryApplyStreamReplacement(pipeline);

		for (auto& uncaptured : gUncapturedPipelineStates)
		{
			if (!uncaptured.replacementPipelineState)
				TryApplyUncapturedReplacement(uncaptured);
		}

		RebuildPipelineStateOverrideMap();
		gShaderTargetApplyDirty = false;
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

					const bool hiddenSelection = gShaderSelectionStyle == ShaderSelectionStyle::Hidden;
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

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| PRESENT |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| PRESENT |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| PRESENT |||||||||||||||||||||||||||||||||||||||||||||||||||||

	static HRESULT HandlePresentD3D12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pParams, bool usePresent1);

	HRESULT STDMETHODCALLTYPE Hook_PresentD3D12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags)
	{
		return HandlePresentD3D12(pSwapChain, SyncInterval, Flags, nullptr, false);
	}

	HRESULT STDMETHODCALLTYPE Hook_Present1D3D12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pParams)
	{
		return HandlePresentD3D12(pSwapChain, SyncInterval, Flags, pParams, true);
	}

	static HRESULT HandlePresentD3D12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pParams, bool usePresent1)
	{
		FPSCounter::UpdateFPSCounter();

		if (usePresent1)
		{
			if (!gLoggedPresent1Hook)
			{
				ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->HandlePresentD3D12: Present1 hook active");
				gLoggedPresent1Hook = true;
			}
		}
		else if (!gLoggedPresentHook)
		{
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->HandlePresentD3D12: Present hook active");
			gLoggedPresentHook = true;
		}

		auto CallOriginalPresent = [&]() -> HRESULT
		{
			if (usePresent1 && Original_Present1D3D12)
				return Original_Present1D3D12(pSwapChain, SyncInterval, Flags, pParams);

			return Original_PresentD3D12(pSwapChain, SyncInterval, Flags);
		};

		gAfterFirstPresent = true;

		if (!gCommandQueue)
		{
			//DebugLog("[HookD3D12] CommandQueue not yet captured, skipping frame");

			if (!gDevice) 
			{
				pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&gDevice);
			}

			return CallOriginalPresent();
		}

		//IMPORTANT NOTE: it appears that when first starting the application gInitialized is false
		//if (gInitialized)
			//ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->HandlePresentD3D12: gInitialized = TRUE");
		//else
			//ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->HandlePresentD3D12: gInitialized = FALSE");
	
		DXGI_SWAP_CHAIN_DESC startupSwapChainDesc = {};

		if (!gInitialized && !IsSwapChainReadyForOverlayInitialization(pSwapChain, startupSwapChainDesc))
			return CallOriginalPresent();

		//IMPORTANT NOTE: we do hit this point after x amount of startup frames where we continue with initalization
		//MessageBoxA(nullptr, "Hook_Present1D3D12: startup frames beyond 300, continuing", "Shader Injector", MB_OK);

		ProcessPendingRebuilds(); // <-- add here, before gInitialized check and before ImGui
		ApplyShaderTargetPSOs();

		///*
		//this will execute first because when application starts, this is not set to true
		if (!gInitialized)
		{
			//ShaderInjectorGUI::WriteToRuntimeLog("[HookD3D12] Initializing ImGui on first Present1.");

			//IMPORTANT NOTE: this seems to pass fortunately, it doesn't fail
			if (!gDevice && FAILED(pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&gDevice))) 
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->HandlePresentD3D12: GetDevice fail");
				return CallOriginalPresent();
			}

			if (!gDevice2)
			{
				HRESULT hr = gDevice->QueryInterface(IID_PPV_ARGS(&gDevice2));

				if (FAILED(hr))
				{
					ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->HandlePresentD3D12: Failed to get ID3D12Device2");
				}
			}

			// Swap Chain description
			DXGI_SWAP_CHAIN_DESC desc = startupSwapChainDesc;

			if (!desc.OutputWindow)
				pSwapChain->GetDesc(&desc);

			gBufferCount = desc.BufferCount;

			// Create descriptor heaps
			D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
			heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			heapDesc.NumDescriptors = gBufferCount;

			//IMPORTANT NOTE: this seems to pass fortunately, it doesn't fail
			if (FAILED(gDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&gHeapRTV)))) 
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->HandlePresentD3D12: CreateDescriptorHeap RTV fail");
				return CallOriginalPresent();
			}

			heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

			//IMPORTANT NOTE: this seems to pass fortunately, it doesn't fail
			if (FAILED(gDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&gHeapSRV)))) 
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->HandlePresentD3D12: CreateDescriptorHeap SRV fail");
				return CallOriginalPresent();
			}

			// Allocate frame contexts
			gFrameContexts = new FrameContext[gBufferCount];
			ZeroMemory(gFrameContexts, sizeof(FrameContext) * gBufferCount);

			// Create command allocator for each frame
			//IMPORTANT NOTE: this seems to pass fortunately, it doesn't fail
			for (UINT i = 0; i < gBufferCount; ++i)
			{
				if (FAILED(gDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gFrameContexts[i].allocator)))) 
				{
					ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->HandlePresentD3D12: CreateCommandAllocator fail");
					return CallOriginalPresent();
				}
			}

			// Create RTVs for each back buffer
			UINT rtvSize = gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			auto rtvHandle = gHeapRTV->GetCPUDescriptorHandleForHeapStart();

			for (UINT i = 0; i < gBufferCount; ++i) 
			{
				ID3D12Resource* back = nullptr;

				//IMPORTANT NOTE: WE DONT HIT THIS ERROR ANYMORE
				//============================ ERROR POINT ===========================
				/*
				The ff7rebirth has crashed and will close
				---------------------------
				LowLevelFatalError [File:Unknown] [Line: 952]
				SwapChain1->ResizeBuffers(NumBackBuffers, SizeX, SizeY, GetRenderTargetFormat(PixelFormat), SwapChainFlags) failed
				 at D:/End/j/workspace/E2/PC/e2p_BuW64MSt/cw/Engine/Source/Runtime/D3D12RHI/Private/Windows/WindowsD3D12Viewport.cpp:453
				 with error DXGI_ERROR_INVALID_CALL
				Num=3, Size=(1920,1080), PF=18, DXGIFormat=0x18, Flags=0x802
				*/
				HRESULT hr = pSwapChain->GetBuffer(i, IID_PPV_ARGS(&back)); //<---------- this is where the error happens

				//this was a supposed fix, apparently the error isn't happening anymore but keeping this around for sanity sake
				//if (back)
				//{
					//back->Release();
					//back = nullptr;
				//}

				if (FAILED(hr))
					continue;

				gDevice->CreateRenderTargetView(back, nullptr, rtvHandle);
				gFrameContexts[i].renderTarget = back;
				gFrameContexts[i].rtvHandle = rtvHandle;
				rtvHandle.ptr += rtvSize;
			}


			// ImGui setup
			//NOTE: we did a test to see if the context of imgui is the same as the one later when we actually draw. it seems that it is infact the same
			ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO(); 
			io.IniFilename = ShaderInjectorIO::imguiSettingsName;

			//NOTE: we did a test here to see if we had fonts (io.Fonts->Fonts.Size), turns out we dont
			
			//FIX: we forcefully add a font
			io.Fonts->AddFontDefault();

			unsigned char* pixels = nullptr;
			int width = 0;
			int height = 0;

			io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

			//NOTE: another test we did here to confirm if after trying to force add fonts to see if we have fonts, and it looks like it worked

			(void)io;
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			ImGui::StyleColorsDark();
			ImGui_ImplWin32_Init(desc.OutputWindow); //this initalizes fine, test report: HWND=00000000003D102A IsWindow=1
			ImGui_ImplDX12_Init(gDevice, gBufferCount,
				desc.BufferDesc.Format, //test: d3d12 desc buffdesc format | format is 24
				gHeapSRV,
				gHeapSRV->GetCPUDescriptorHandleForHeapStart(),
				gHeapSRV->GetGPUDescriptorHandleForHeapStart()); //this also seemingly initalizes fine

			//IMPORTANT NOTE: this seems to pass fortunately, it doesn't fail
			//ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->HandlePresentD3D12: ImGui initialized");

			HookInput::Initalize(desc.OutputWindow);

			if (!gOverlayFence) 
			{
				//IMPORTANT NOTE: this seems to pass fortunately, it doesn't fail
				if (FAILED(gDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gOverlayFence)))) 
				{
					ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->HandlePresentD3D12: CreateFence fail");
					return CallOriginalPresent();
				}
			}

			if (!gFenceEvent) 
			{
				gFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

				//IMPORTANT NOTE: this seems to pass fortunately, it doesn't fail
				if (!gFenceEvent)
				{
					char buffer[256];
					sprintf_s(buffer, "HookD3D12->HandlePresentD3D12: Failed to create fence event: %lu", GetLastError());
					ShaderInjectorGUI::WriteToRuntimeLogError(buffer);
				}
			}

			GatherPipelineInfo(pSwapChain);
			InstallPipelineHooks();
		
			// Hook CommandQueue and Fence are already captured by minhook
			gInitialized = true;

			if (!gOverlayInitializedTick)
				gOverlayInitializedTick = GetTickCount64();

			if (!gLoggedOverlayInitialized)
			{
				ShaderInjectorIO::WriteToLogFile("HookD3D12->HandlePresentD3D12: Overlay initialized from present path");
				gLoggedOverlayInitialized = true;
			}
		}

		InstallCommandListHooks();


		if (!Globals::gShowShaderInjectorGUI || gOverlayRenderingDisabled)
			return CallOriginalPresent();

		if (gOverlayInitializedTick && GetTickCount64() - gOverlayInitializedTick < 5000)
		{
			if (!gLoggedStartupMenuDelay)
			{
				ShaderInjectorIO::WriteToLogFile("HookD3D12->HandlePresentD3D12: delaying startup menu render until overlay is stable");
				gLoggedStartupMenuDelay = true;
			}

			return CallOriginalPresent();
		}

		if (!gShutdown) 
		{
			// Render ImGui
			ImGui_ImplDX12_NewFrame(); //this seems fine
			ImGui_ImplWin32_NewFrame(); //this seems fine

			ImGuiContext* imguiContext = ImGui::GetCurrentContext();
			ImGuiIO& io = ImGui::GetIO();
			io.MouseDrawCursor = true;
			io.IniFilename = ShaderInjectorIO::imguiSettingsName;

			//NOTE: we did a test earlier, checking if the imgui context we created during initalization is the same as the one now by checking the address, they are infact the same
			//NOTE: we also did a test here to see if there was any fonts, there wasn't, test result: ctx=000001FCF04F71C0 fonts=000001FCF04F18D0 fontcount=0

			//IMPORTANT: safety check to ensure that we have fonts, if we don't we hit a fatal application error
			if (io.Fonts->Fonts.Size == 0)
			{
				MessageBoxA(nullptr, "NO FONTS", "Shader Injector", MB_OK);

				//dont continue otherwise hell breaks loose
				return CallOriginalPresent();
			}

			//NOTE: we did a test in the past, and we did used to hit a fatal app error here, but not anymore
			ImGui::NewFrame();

			//=========================================== IMGUI WINDOW START ===========================================

			ShaderInjectorGUI::MainWindowContext guiContext{};
			guiContext.showWindow = &Globals::gShowShaderInjectorGUI;
			guiContext.injectorEnabled = Globals::gShaderInjectorEnabled;
			guiContext.fpsCounterActive = &FPSCounter::gIsFPSCounterActive;
			guiContext.fps = FPSCounter::gCurrentFramesPerSecond;
			guiContext.frameTimeMs = FPSCounter::gCurrentFrameTimeMilliseconds;
			guiContext.runtimeLogText = &ShaderInjectorGUI::runtimeLogText;
			guiContext.drawMenu = &ShaderInjectorGUI::UI_ShaderInjectorMenu;
			ShaderInjectorGUI::DrawMainWindow(guiContext);

			//=========================================== IMGUI END ===========================================

			UINT frameIdx = pSwapChain->GetCurrentBackBufferIndex();

			if (!gFrameContexts || gBufferCount == 0 || frameIdx >= gBufferCount)
			{
				ImGui::EndFrame();
				return CallOriginalPresent();
			}

			FrameContext& ctx = gFrameContexts[frameIdx];

			if (!ctx.allocator || !ctx.renderTarget)
			{
				ImGui::EndFrame();
				return CallOriginalPresent();
			}

			// Wait for the GPU to finish with the previous frame
			bool canRender = true;

			if (!gOverlayFence || !gFenceEvent) 
			{
				// Missing synchronization objects, skip waiting
			}
			else if (gOverlayFence->GetCompletedValue() < gOverlayFenceValue) 
			{
				HRESULT hr = gOverlayFence->SetEventOnCompletion(gOverlayFenceValue, gFenceEvent);

				if (SUCCEEDED(hr)) 
				{
					const DWORD waitTimeoutMs = 0; // Never stall the game present path for overlay rendering
					DWORD waitRes = WaitForSingleObject(gFenceEvent, waitTimeoutMs);

					if (waitRes == WAIT_TIMEOUT)
					{
						//ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->HandlePresentD3D12: WaitForSingleObject timeout");
						canRender = false;
					}
					else if (waitRes != WAIT_OBJECT_0) 
					{
						//ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->HandlePresentD3D12: WaitForSingleObject failed: %lu", GetLastError());
						canRender = false;
					}
				}
				else 
				{
					//LogHRESULT("SetEventOnCompletion", hr);
					canRender = false;
				}
			}

			if (!canRender) 
			{
				//ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->HandlePresentD3D12: Skipping ImGui render for this frame");
				ImGui::EndFrame();
				return CallOriginalPresent();
			}

			// Reset allocator and command list using frame-specific allocator
			HRESULT hr = ctx.allocator->Reset();

			if (FAILED(hr)) 
			{
				//LogHRESULT("CommandAllocator->Reset", hr);
				ImGui::EndFrame();
				return CallOriginalPresent();
			}

			if (!gCommandList) 
			{
				hr = gDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ctx.allocator, nullptr, IID_PPV_ARGS(&gCommandList));
				
				if (FAILED(hr)) 
				{
					//LogHRESULT("CreateCommandList", hr);
					ImGui::EndFrame();
					return CallOriginalPresent();
				}

				gCommandList->Close();
			}

			hr = gCommandList->Reset(ctx.allocator, nullptr);

			if (FAILED(hr)) 
			{
				//LogHRESULT("CommandList->Reset", hr);
				ImGui::EndFrame();
				return CallOriginalPresent();
			}

			// Transition to render target
			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = ctx.renderTarget;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			gCommandList->ResourceBarrier(1, &barrier);

			gCommandList->OMSetRenderTargets(1, &ctx.rtvHandle, FALSE, nullptr);
			ID3D12DescriptorHeap* heaps[] = { gHeapSRV };
			gCommandList->SetDescriptorHeaps(1, heaps);

			ImGui::Render();
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), gCommandList);

			// Transition back to present
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			gCommandList->ResourceBarrier(1, &barrier);
			gCommandList->Close();

			// Execute
			if (!gCommandQueue) 
			{
				//ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->HandlePresentD3D12: CommandQueue not set, skipping ExecuteCommandLists.");
			}
			else
			{
				Original_ExecuteCommandListsD3D12(gCommandQueue, 1, reinterpret_cast<ID3D12CommandList* const*>(&gCommandList));

				if (gOverlayFence) 
				{
					// Call Signal directly on the command queue to synchronize the internal overlay.
					HRESULT hr = gCommandQueue->Signal(gOverlayFence, ++gOverlayFenceValue);

					if (FAILED(hr)) 
					{
						//LogHRESULT("Signal", hr);

						if (gDevice) 
						{
							HRESULT reason = gDevice->GetDeviceRemovedReason();
							//DebugLog("[HookD3D12] DeviceRemovedReason=0x%08X", reason);

							if (reason != S_OK) 
							{
								//DebugLog("[HookD3D12] Device lost. Releasing resources.");
								Release();
							}
						}
					}
				}
			}
		}

		return CallOriginalPresent();
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| EXECUTE COMMAND LISTS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| EXECUTE COMMAND LISTS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| EXECUTE COMMAND LISTS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	void STDMETHODCALLTYPE Hook_ExecuteCommandListsD3D12(ID3D12CommandQueue* _this, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists)
	{
		if (!gCommandQueue && gAfterFirstPresent) 
		{
			ID3D12Device* queueDevice = nullptr;

			if (SUCCEEDED(_this->GetDevice(__uuidof(ID3D12Device), (void**)&queueDevice))) 
			{
				if (!gDevice)
				{
					gDevice = queueDevice;
				}

				if (queueDevice == gDevice) 
				{
					D3D12_COMMAND_QUEUE_DESC desc = _this->GetDesc();
					//DebugLog("[HookD3D12] CommandQueue type=%u", desc.Type);

					if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) 
					{
						_this->AddRef();
						gCommandQueue = _this;
						if (!gLoggedCommandQueueCaptured)
						{
							//ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->Hook_ExecuteCommandListsD3D12: Captured direct command queue");
							gLoggedCommandQueueCaptured = true;
						}
						//DebugLog("[HookD3D12] Captured CommandQueue=%p", _this);
					}
					else 
					{
						//DebugLog("[HookD3D12] Skipping capture: non-direct queue");
					}
				}
				else 
				{
					//DebugLog("[HookD3D12] Skipping capture: CommandQueue uses different device (%p != %p)", queueDevice, gDevice);
				}

				if (queueDevice && queueDevice != gDevice)
					queueDevice->Release();
			}
		}

		gAfterFirstPresent = false;
		Original_ExecuteCommandListsD3D12(_this, NumCommandLists, ppCommandLists);
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| RESIZE BUFFERS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| RESIZE BUFFERS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| RESIZE BUFFERS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	static bool WaitForOverlayGPUIdle(DWORD timeoutMs = 2000)
	{
		if (!gCommandQueue || !gOverlayFence || !gFenceEvent)
			return true;

		++gOverlayFenceValue;

		HRESULT hr = gCommandQueue->Signal(gOverlayFence, gOverlayFenceValue);

		if (FAILED(hr))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->WaitForOverlayGPUIdle: Overlay fence signal failed during resize teardown.");
			return false;
		}

		if (gOverlayFence->GetCompletedValue() >= gOverlayFenceValue)
			return true;

		hr = gOverlayFence->SetEventOnCompletion(gOverlayFenceValue, gFenceEvent);

		if (FAILED(hr))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->WaitForOverlayGPUIdle: Overlay fence SetEventOnCompletion failed during resize teardown.");
			return false;
		}

		DWORD waitResult = WaitForSingleObject(gFenceEvent, timeoutMs);

		if (waitResult != WAIT_OBJECT_0)
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12->WaitForOverlayGPUIdle: Overlay fence wait timed out during resize teardown; releasing overlay resources anyway.");
			return false;
		}

		return true;
	}

	static void ShutdownImGuiOverlayBackend()
	{
		if (!ImGui::GetCurrentContext())
			return;

		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}

	static void ReleaseOverlaySwapChainResources(bool shutdownImGuiBackend)
	{
		gInitialized = false;

		if (shutdownImGuiBackend)
			ShutdownImGuiOverlayBackend();

		if (gCommandList)
		{
			gCommandList->Release();
			gCommandList = nullptr;
		}

		if (gFrameContexts)
		{
			for (UINT i = 0; i < gBufferCount; ++i)
			{
				if (gFrameContexts[i].renderTarget)
				{
					gFrameContexts[i].renderTarget->Release();
					gFrameContexts[i].renderTarget = nullptr;
				}

				if (gFrameContexts[i].allocator)
				{
					gFrameContexts[i].allocator->Release();
					gFrameContexts[i].allocator = nullptr;
				}
			}

			delete[] gFrameContexts;
			gFrameContexts = nullptr;
		}

		if (gHeapRTV)
		{
			gHeapRTV->Release();
			gHeapRTV = nullptr;
		}

		if (gHeapSRV)
		{
			gHeapSRV->Release();
			gHeapSRV = nullptr;
		}

		gBufferCount = 0;
		gInitialized = false;
	}

	//NOT USED YET, but should be in the future, otherwise we get fatal errors when the app window resizes
	//in the case of rebirth for example with ffviihook, any commands that change the window/render size will trigger this
	HRESULT STDMETHODCALLTYPE Hook_ResizeBuffersD3D12(
		IDXGISwapChain3* pSwapChain,
		UINT BufferCount,
		UINT Width,
		UINT Height,
		DXGI_FORMAT NewFormat,
		UINT SwapChainFlags)
	{
		char buffer[1024];
		sprintf_s(buffer, "HookD3D12->Hook_ResizeBuffersD3D12: ResizeBuffers %ux%u Buffers=%u", Width, Height, BufferCount);
		ShaderInjectorGUI::WriteToRuntimeLog(buffer);

		// Release every overlay object that depends on swap-chain buffers or descriptor heaps before ResizeBuffers.
		// ImGui owns a font texture/SRV through the DX12 backend, so a partial back-buffer release can corrupt the UI after resize.
		gOverlayRenderingDisabled = true;

		if (gInitialized || gFrameContexts || gHeapRTV || gHeapSRV || gCommandList)
		{
			WaitForOverlayGPUIdle();
			ReleaseOverlaySwapChainResources(true);
		}

		HRESULT hr = Original_ResizeBuffersD3D12(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

		if (FAILED(hr))
		{
			sprintf_s(buffer, "HookD3D12->Hook_ResizeBuffersD3D12: ResizeBuffers FAILED hr=0x%08X", (UINT)hr);
			ShaderInjectorGUI::WriteToRuntimeLogError(buffer);
			gOverlayRenderingDisabled = false;
			ResetOverlayStartupGate();

			return hr;
		}

		//
		// Force reinitialization next Present().
		//
		gBufferCount = 0;
		gInitialized = false;
		gOverlayRenderingDisabled = false;
		NotifyOverlayResizeBuffersSucceeded();

		ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->Hook_ResizeBuffersD3D12: ResizeBuffers succeeded. Overlay recreation deferred until resize settles.");

		return hr;
	}

	void Release()
	{
		//ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12->Release: Releasing resources and hooks.");

		gShutdown = true;
		ResetOverlayStartupGate();

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

		ReleaseRootSignatureCache();
		gCurrentGraphicsRootSignatureByCommandList.clear();
		gCurrentComputeRootSignatureByCommandList.clear();

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