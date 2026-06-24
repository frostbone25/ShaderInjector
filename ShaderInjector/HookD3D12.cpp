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
#include "PreCompiledHeader.h" //stupid precompiled header
#include "Hooks.h"
#include "Globals.h"
#include "dsound_proxy.h"
#include "HookD3D12.h"
#include "HookInput.h"
#include "ShaderInjectorIO.h"
#include "ShaderReplacement.h"
#include "ShaderInjectorGUI.h"
#include "Hash.h"
#include "FPSCounter.h"
#include "HookD3D12PipelineUtils.h"
#include "VTableIndex.h"

#if defined _M_X64
typedef uint64_t uintx_t;
#elif defined _M_IX86
typedef uint32_t uintx_t;
#endif

namespace HookD3D12
{
	PresentD3D12                  Original_PresentD3D12 = nullptr;
	Present1Fn                    Original_Present1D3D12 = nullptr;
	D3D12CreateDeviceFn           Original_D3D12CreateDevice = nullptr;
	ExecuteCommandListsFn         Original_ExecuteCommandListsD3D12 = nullptr;
	ResizeBuffersFn               Original_ResizeBuffersD3D12 = nullptr;
	PFN_CreatePipelineLibrary     Original_CreatePipelineLibrary = nullptr;
	PFN_LoadGraphicsPipeline      Original_LoadGraphicsPipeline = nullptr;
	PFN_LoadComputePipeline       Original_LoadComputePipeline = nullptr;
	PFN_LoadPipeline              Original_LoadPipeline = nullptr;
	PFN_StorePipeline             Original_StorePipeline = nullptr;
	PFN_GetSerializedSize         Original_GetSerializedSize = nullptr;
	PFN_Serialize                 Original_Serialize = nullptr;
	SetPipelineStateFn            Original_SetPipelineState = nullptr;
	ResetGraphicsCommandListFn    Original_ResetGraphicsCommandList = nullptr;
	SetGraphicsRootSignatureFn    Original_SetGraphicsRootSignature = nullptr;
	SetComputeRootSignatureFn     Original_SetComputeRootSignature = nullptr;
	CreateComputePipelineStateFn  Original_CreateComputePipelineState = nullptr;
	CreateRootSignatureFn         Original_CreateRootSignature = nullptr;
	CreateGraphicsPipelineStateFn Original_CreateGraphicsPipelineState = nullptr;
	CreatePipelineStateFn         Original_CreatePipelineState = nullptr;

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
	static bool          gOverlayRenderingDisabled = false;
	static ULONGLONG     gLastResizeBuffersTick = 0;
	static bool          gLoggedResizeCooldown = false;
	static bool          gAfterFirstPresent = false;
	static bool          gLoggedPresentHook = false;
	static bool          gLoggedPresent1Hook = false;
	static bool          gLoggedCommandQueueCaptured = false;
	static bool          gLoggedOverlayInitialized = false;

	static bool gCommandListHookInstalled = false;
	static bool gD3D12CreateDeviceHookInstalled = false;
	static std::unordered_set<void*> gPipelineHookedDeviceVTables;
	static std::unordered_set<ID3D12PipelineState*> gUntrackedBoundPipelineStates;
	static std::unordered_set<ID3D12PipelineState*> gKnownPipelineStates;
	static std::vector<UncapturedPipelineStateInfo> gUncapturedPipelineStates;
	static std::unordered_set<void*> gCommandListHookedVTables;
	static std::unordered_map<ID3D12GraphicsCommandList*, ID3D12RootSignature*> gCurrentGraphicsRootSignatureByCommandList;
	static std::unordered_map<ID3D12GraphicsCommandList*, ID3D12RootSignature*> gCurrentComputeRootSignatureByCommandList;
	static std::unordered_map<ID3D12RootSignature*, RootSignatureInfo> gRootSignatureInfoByPointer;
	static std::unordered_map<std::string, ID3D12RootSignature*> gPersistedRootSignaturesByPath;

	struct OverlayStartupGateState
	{
		HWND outputWindow = nullptr;
		UINT bufferCount = 0;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		UINT flags = 0;
		LONG clientWidth = 0;
		LONG clientHeight = 0;
		int stableFrames = 0;
		ULONGLONG firstStableTick = 0;
	};

	static OverlayStartupGateState gOverlayStartupGate;
	static constexpr int kOverlayStartupStableFrameLimit = 5;
	static constexpr ULONGLONG kOverlayStartupMinimumStableMs = 500;
	static constexpr ULONGLONG kOverlayResizeCooldownMs = 2500;
	std::vector<PSOPendingRebuild> gPendingRebuilds;

	static std::mutex gPipelineMutex;
	std::vector<GraphicsPipelineInfo> gGraphicsPipelines;
	static std::vector<ComputePipelineInfo> gComputePipelines;
	D3D12PipelineInfo gPipelineInfo;
	std::vector<PipelineStateInfo> gPipelineStates;

	std::vector<ShaderReplacement::ShaderReplacementDisk> gLoadedShaderReplacements;
	std::vector<std::vector<uint8_t>> gLoadedShaderReplacementBlobs;
	static std::unordered_map<ID3D12PipelineState*, ID3D12PipelineState*> gPipelineStateOverrides;

	static bool gShaderReplacementApplyDirty = true;
	static bool gPipelineStateOverridesDirty = true;
	int gSelectedShaderReplacementIndex = -1;
	int gShaderReplacementNameBufferIndex = -1;
	char gShaderReplacementNameBuffer[256]{};
	bool gLoadedShaderReplacementsOnce = false;
	ShaderSelectionStyle gShaderSelectionStyle = ShaderSelectionStyle::BluePixelShader;

	std::unordered_set<ID3D12PipelineLibrary*> g_HookedLibraries;

	static void ResetOverlayStartupGate()
	{
		gOverlayStartupGate = {};
	}

	static bool ProbeSwapChainBuffers(IDXGISwapChain3* swapChain, UINT bufferCount)
	{
		if (!swapChain || bufferCount == 0)
			return false;

		for (UINT i = 0; i < bufferCount; ++i)
		{
			ID3D12Resource* backBuffer = nullptr;
			HRESULT hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer));

			if (backBuffer)
				backBuffer->Release();

			if (FAILED(hr))
				return false;
		}

		return true;
	}

	static bool IsSwapChainReadyForOverlayInitialization(IDXGISwapChain3* swapChain, DXGI_SWAP_CHAIN_DESC& outDesc)
	{
		outDesc = {};

		if (!swapChain)
		{
			ResetOverlayStartupGate();
			return false;
		}

		HRESULT hr = swapChain->GetDesc(&outDesc);
		if (FAILED(hr))
		{
			ResetOverlayStartupGate();
			return false;
		}

		HWND outputWindow = outDesc.OutputWindow;
		if (!outputWindow || !IsWindow(outputWindow) || IsIconic(outputWindow))
		{
			ResetOverlayStartupGate();
			return false;
		}

		if (outDesc.BufferCount == 0 || outDesc.BufferDesc.Format == DXGI_FORMAT_UNKNOWN)
		{
			ResetOverlayStartupGate();
			return false;
		}

		RECT clientRect{};
		if (!GetClientRect(outputWindow, &clientRect))
		{
			ResetOverlayStartupGate();
			return false;
		}

		LONG clientWidth = clientRect.right - clientRect.left;
		LONG clientHeight = clientRect.bottom - clientRect.top;
		if (clientWidth <= 0 || clientHeight <= 0)
		{
			ResetOverlayStartupGate();
			return false;
		}

		ULONGLONG now = GetTickCount64();
		if (gLastResizeBuffersTick != 0 && now - gLastResizeBuffersTick < kOverlayResizeCooldownMs)
		{
			if (!gLoggedResizeCooldown)
			{
				ShaderInjectorGUI::WriteToRuntimeLog("[HookD3D12] Resize cooldown active; delaying overlay recreation.");
				gLoggedResizeCooldown = true;
			}

			return false;
		}

		gLoggedResizeCooldown = false;

		bool swapChainChanged =
			gOverlayStartupGate.outputWindow != outputWindow ||
			gOverlayStartupGate.bufferCount != outDesc.BufferCount ||
			gOverlayStartupGate.format != outDesc.BufferDesc.Format ||
			gOverlayStartupGate.flags != outDesc.Flags ||
			gOverlayStartupGate.clientWidth != clientWidth ||
			gOverlayStartupGate.clientHeight != clientHeight;

		if (swapChainChanged)
		{
			gOverlayStartupGate.outputWindow = outputWindow;
			gOverlayStartupGate.bufferCount = outDesc.BufferCount;
			gOverlayStartupGate.format = outDesc.BufferDesc.Format;
			gOverlayStartupGate.flags = outDesc.Flags;
			gOverlayStartupGate.clientWidth = clientWidth;
			gOverlayStartupGate.clientHeight = clientHeight;
			gOverlayStartupGate.stableFrames = 1;
			gOverlayStartupGate.firstStableTick = now;
			return false;
		}

		if (!ProbeSwapChainBuffers(swapChain, outDesc.BufferCount))
		{
			gOverlayStartupGate.stableFrames = 0;
			gOverlayStartupGate.firstStableTick = now;
			return false;
		}

		++gOverlayStartupGate.stableFrames;
		return gOverlayStartupGate.stableFrames >= kOverlayStartupStableFrameLimit &&
			(now - gOverlayStartupGate.firstStableTick) >= kOverlayStartupMinimumStableMs;
	}

	bool GetPipelineCachedBlobInfo(ID3D12PipelineState* pipelineState, uint64_t& outHash, SIZE_T& outSize, std::vector<uint8_t>* outBytes = nullptr)
	{
		outHash = 0;
		outSize = 0;

		if (outBytes)
			outBytes->clear();

		if (!pipelineState)
			return false;

		ID3DBlob* cachedBlob = nullptr;
		HRESULT hr = pipelineState->GetCachedBlob(&cachedBlob);

		if (FAILED(hr) || !cachedBlob)
			return false;

		const void* blobData = cachedBlob->GetBufferPointer();
		const SIZE_T blobSize = cachedBlob->GetBufferSize();

		if (blobData && blobSize > 0)
		{
			outHash = Hash::HashMemory(blobData, blobSize);
			outSize = blobSize;

			if (outBytes)
			{
				const uint8_t* bytes = static_cast<const uint8_t*>(blobData);
				outBytes->assign(bytes, bytes + blobSize);
			}
		}

		cachedBlob->Release();
		return outHash != 0;
	}

		bool ReplacementHasCachedBlobSize(const ShaderReplacement::ShaderReplacementDisk& replacement, SIZE_T cachedBlobSize)
	{
		if (!cachedBlobSize)
			return false;

		if (!replacement.pipelineCachedBlobLength.empty())
		{
			const SIZE_T replacementSize = (SIZE_T)_strtoui64(replacement.pipelineCachedBlobLength.c_str(), nullptr, 10);
			if (replacementSize == cachedBlobSize)
				return true;
		}

		for (const ShaderReplacement::ShaderPipelineTemplateDisk& pipelineTemplate : replacement.pipelineTemplates)
		{
			if (pipelineTemplate.pipelineCachedBlobLength.empty())
				continue;

			const SIZE_T templateSize = (SIZE_T)_strtoui64(pipelineTemplate.pipelineCachedBlobLength.c_str(), nullptr, 10);
			if (templateSize == cachedBlobSize)
				return true;
		}

		return false;
	}

	bool ReplacementHasCachedBlobHash(const ShaderReplacement::ShaderReplacementDisk& replacement, uint64_t cachedBlobHash)
	{
		if (!cachedBlobHash)
			return false;

		if (Hash::ParseHashText(replacement.pipelineCachedBlobHash) == cachedBlobHash)
			return true;

		for (const ShaderReplacement::ShaderPipelineTemplateDisk& pipelineTemplate : replacement.pipelineTemplates)
		{
			if (Hash::ParseHashText(pipelineTemplate.pipelineCachedBlobHash) == cachedBlobHash)
				return true;
		}

		return false;
	}

	int CountEnabledShaderReplacementsByCachedBlobSize(SIZE_T cachedBlobSize)
	{
		if (!cachedBlobSize)
			return 0;

		int matchCount = 0;
		for (const auto& replacement : gLoadedShaderReplacements)
		{
			if (!replacement.enabled)
				continue;

			if (ReplacementHasCachedBlobSize(replacement, cachedBlobSize))
				matchCount++;
		}

		return matchCount;
	}

	int FindUniqueEnabledShaderReplacementByCachedBlobSize(SIZE_T cachedBlobSize)
	{
		if (!cachedBlobSize)
			return -1;

		int matchIndex = -1;
		int matchCount = 0;

		for (int i = 0; i < (int)gLoadedShaderReplacements.size(); i++)
		{
			const ShaderReplacement::ShaderReplacementDisk& replacement = gLoadedShaderReplacements[i];

			if (!replacement.enabled)
				continue;

			if (ReplacementHasCachedBlobSize(replacement, cachedBlobSize))
			{
				matchIndex = i;
				matchCount++;
			}
		}

		return matchCount == 1 ? matchIndex : -1;
	}

	int FindEnabledShaderReplacementByCachedBlob(uint64_t cachedBlobHash)
	{
		if (!cachedBlobHash)
			return -1;

		for (int i = 0; i < (int)gLoadedShaderReplacements.size(); i++)
		{
			const ShaderReplacement::ShaderReplacementDisk& replacement = gLoadedShaderReplacements[i];

			if (!replacement.enabled)
				continue;

			if (ReplacementHasCachedBlobHash(replacement, cachedBlobHash))
				return i;
		}

		return -1;
	}

	bool ReplacementHashMatches(uint64_t pipelineHash, const std::string& replacementHash)
	{
		const uint64_t parsedHash = Hash::ParseHashText(replacementHash);
		return parsedHash == 0 || parsedHash == pipelineHash;
	}

	bool GraphicsPipelineMatchesReplacementTemplate(const GraphicsPipelineInfo& pipeline, const ShaderReplacement::ShaderReplacementDisk& replacement)
	{
		return ReplacementHashMatches(pipeline.vsHash, replacement.vsHash) &&
			ReplacementHashMatches(pipeline.psHash, replacement.psHash) &&
			ReplacementHashMatches(pipeline.gsHash, replacement.gsHash) &&
			ReplacementHashMatches(pipeline.hsHash, replacement.hsHash) &&
			ReplacementHashMatches(pipeline.dsHash, replacement.dsHash);
	}

	bool StreamPipelineMatchesReplacementTemplate(const PipelineStateInfo& pipeline, const ShaderReplacement::ShaderReplacementDisk& replacement)
	{
		return ReplacementHashMatches(pipeline.vsHash, replacement.vsHash) &&
			ReplacementHashMatches(pipeline.psHash, replacement.psHash) &&
			ReplacementHashMatches(pipeline.csHash, replacement.csHash) &&
			ReplacementHashMatches(pipeline.gsHash, replacement.gsHash) &&
			ReplacementHashMatches(pipeline.hsHash, replacement.hsHash) &&
			ReplacementHashMatches(pipeline.dsHash, replacement.dsHash);
	}

	D3D12_PIPELINE_STATE_SUBOBJECT_TYPE SubobjectTypeForShaderType(ShaderReplacement::ShaderType shaderType)
	{
		switch (shaderType)
		{
			case ShaderReplacement::VertexShader:   return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS;
			case ShaderReplacement::PixelShader:    return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS;
			case ShaderReplacement::GeometryShader: return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS;
			case ShaderReplacement::HullShader:     return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS;
			case ShaderReplacement::DomainShader:   return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS;
			case ShaderReplacement::ComputeShader:  return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS;
			default: return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MAX_VALID;
		}
	}

	int FindEnabledShaderReplacement(uint64_t shaderHash, ShaderReplacement::ShaderType shaderType)
	{
		if (!shaderHash)
			return -1;

		for (int i = 0; i < (int)gLoadedShaderReplacements.size(); i++)
		{
			const ShaderReplacement::ShaderReplacementDisk& replacement = gLoadedShaderReplacements[i];

			if (!replacement.enabled || replacement.shaderType != shaderType)
				continue;

			if (Hash::ParseHashText(replacement.originalShaderBytecodeHash) == shaderHash)
				return i;
		}

		return -1;
	}

	bool ReplacementStillEnabled(const std::string& replacementName, uint64_t shaderHash, ShaderReplacement::ShaderType shaderType)
	{
		const int replacementIndex = FindEnabledShaderReplacement(shaderHash, shaderType);

		if (replacementIndex < 0)
			return false;

		return gLoadedShaderReplacements[replacementIndex].name == replacementName;
	}

	void RegisterKnownPipelineStateLocked(ID3D12PipelineState* pipelineStateObject)
	{
		if (pipelineStateObject)
			gKnownPipelineStates.insert(pipelineStateObject);
	}

	void UnregisterKnownPipelineStateLocked(ID3D12PipelineState* pipelineStateObject)
	{
		if (pipelineStateObject)
			gKnownPipelineStates.erase(pipelineStateObject);
	}

	void MarkShaderReplacementApplyDirty()
	{
		gShaderReplacementApplyDirty = true;
		gPipelineStateOverridesDirty = true;
	}

	void CaptureGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, ID3D12PipelineState* pipelineState)
	{
		if (!desc || !pipelineState)
			return;

		GraphicsPipelineInfo info{};
		info.pipelineState = pipelineState;

		if (desc->VS.pShaderBytecode && desc->VS.BytecodeLength)
		{
			info.vsHash = Hash::HashMemory(desc->VS.pShaderBytecode, desc->VS.BytecodeLength);
			info.vsSize = desc->VS.BytecodeLength;
			info.vsBytecode.assign((const uint8_t*)desc->VS.pShaderBytecode, (const uint8_t*)desc->VS.pShaderBytecode + desc->VS.BytecodeLength);
		}

		if (desc->PS.pShaderBytecode && desc->PS.BytecodeLength)
		{
			info.psHash = Hash::HashMemory(desc->PS.pShaderBytecode, desc->PS.BytecodeLength);
			info.psSize = desc->PS.BytecodeLength;
			info.psBytecode.assign((const uint8_t*)desc->PS.pShaderBytecode, (const uint8_t*)desc->PS.pShaderBytecode + desc->PS.BytecodeLength);
		}

		if (desc->GS.pShaderBytecode && desc->GS.BytecodeLength)
		{
			info.gsHash = Hash::HashMemory(desc->GS.pShaderBytecode, desc->GS.BytecodeLength);
			info.gsSize = desc->GS.BytecodeLength;
			info.gsBytecode.assign((const uint8_t*)desc->GS.pShaderBytecode, (const uint8_t*)desc->GS.pShaderBytecode + desc->GS.BytecodeLength);
		}

		if (desc->HS.pShaderBytecode && desc->HS.BytecodeLength)
		{
			info.hsHash = Hash::HashMemory(desc->HS.pShaderBytecode, desc->HS.BytecodeLength);
			info.hsSize = desc->HS.BytecodeLength;
			info.hsBytecode.assign((const uint8_t*)desc->HS.pShaderBytecode, (const uint8_t*)desc->HS.pShaderBytecode + desc->HS.BytecodeLength);
		}

		if (desc->DS.pShaderBytecode && desc->DS.BytecodeLength)
		{
			info.dsHash = Hash::HashMemory(desc->DS.pShaderBytecode, desc->DS.BytecodeLength);
			info.dsSize = desc->DS.BytecodeLength;
			info.dsBytecode.assign((const uint8_t*)desc->DS.pShaderBytecode, (const uint8_t*)desc->DS.pShaderBytecode + desc->DS.BytecodeLength);
		}

		info.originalDesc = *desc;
		info.originalDesc.VS = { info.vsBytecode.empty() ? nullptr : info.vsBytecode.data(), info.vsBytecode.size() };
		info.originalDesc.PS = { info.psBytecode.empty() ? nullptr : info.psBytecode.data(), info.psBytecode.size() };
		info.originalDesc.GS = { info.gsBytecode.empty() ? nullptr : info.gsBytecode.data(), info.gsBytecode.size() };
		info.originalDesc.HS = { info.hsBytecode.empty() ? nullptr : info.hsBytecode.data(), info.hsBytecode.size() };
		info.originalDesc.DS = { info.dsBytecode.empty() ? nullptr : info.dsBytecode.data(), info.dsBytecode.size() };
		info.originalDesc.pRootSignature = desc->pRootSignature;

		if (info.originalDesc.pRootSignature)
			info.originalDesc.pRootSignature->AddRef();

		if (desc->InputLayout.pInputElementDescs && desc->InputLayout.NumElements > 0)
		{
			info.inputElements.assign(desc->InputLayout.pInputElementDescs, desc->InputLayout.pInputElementDescs + desc->InputLayout.NumElements);
			info.originalDesc.InputLayout.pInputElementDescs = info.inputElements.data();
			info.originalDesc.InputLayout.NumElements = (UINT)info.inputElements.size();
		}
		else
		{
			info.originalDesc.InputLayout.pInputElementDescs = nullptr;
			info.originalDesc.InputLayout.NumElements = 0;
		}

		if (desc->StreamOutput.pSODeclaration && desc->StreamOutput.NumEntries > 0)
		{
			info.soDeclarations.assign(desc->StreamOutput.pSODeclaration, desc->StreamOutput.pSODeclaration + desc->StreamOutput.NumEntries);
			info.originalDesc.StreamOutput.pSODeclaration = info.soDeclarations.data();
		}
		else
		{
			info.originalDesc.StreamOutput.pSODeclaration = nullptr;
			info.originalDesc.StreamOutput.NumEntries = 0;
		}

		if (desc->StreamOutput.pBufferStrides && desc->StreamOutput.NumStrides > 0)
		{
			info.soStrides.assign(desc->StreamOutput.pBufferStrides, desc->StreamOutput.pBufferStrides + desc->StreamOutput.NumStrides);
			info.originalDesc.StreamOutput.pBufferStrides = info.soStrides.data();
		}
		else
		{
			info.originalDesc.StreamOutput.pBufferStrides = nullptr;
			info.originalDesc.StreamOutput.NumStrides = 0;
		}

		info.originalDesc.CachedPSO.pCachedBlob = nullptr;
		info.originalDesc.CachedPSO.CachedBlobSizeInBytes = 0;

		std::lock_guard<std::mutex> lock(gPipelineMutex);
		RegisterKnownPipelineStateLocked(pipelineState);
		gGraphicsPipelines.push_back(info);
		MarkShaderReplacementApplyDirty();
	}


	void BackfillReplacementCachedBlobInfo(ShaderReplacement::ShaderReplacementDisk& replacement, ID3D12PipelineState* pipelineState)
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
			replacement.pipelineCachedBlobPath = replacement.replacementDirectory + "\\OriginalPipelineCachedBlob" + ShaderInjectorIO::extensionBIN;
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
			MarkShaderReplacementApplyDirty();
	}

	void ReleaseMarkerPSO(ID3D12PipelineState*& pso)
	{
		if (!pso)
			return;

		UnregisterKnownPipelineStateLocked(pso);
		pso->Release();
		pso = nullptr;
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

		MarkShaderReplacementApplyDirty();
	}

	void ClearReplacementPSO(GraphicsPipelineInfo& pipeline)
	{
		if (pipeline.psoWithReplacement)
		{
			UnregisterKnownPipelineStateLocked(pipeline.psoWithReplacement);
			pipeline.psoWithReplacement->Release();
			pipeline.psoWithReplacement = nullptr;
		}

		pipeline.activeShaderReplacementName.clear();
		pipeline.activeShaderReplacementType = ShaderReplacement::Unknown;
		pipeline.activeShaderReplacementHash = 0;
		pipeline.activeShaderReplacementUsesFallback = false;
	}

	void ClearReplacementPSO(PipelineStateInfo& pipeline)
	{
		if (pipeline.psoWithReplacement)
		{
			UnregisterKnownPipelineStateLocked(pipeline.psoWithReplacement);
			pipeline.psoWithReplacement->Release();
			pipeline.psoWithReplacement = nullptr;
		}

		pipeline.activeShaderReplacementName.clear();
		pipeline.activeShaderReplacementType = ShaderReplacement::Unknown;
		pipeline.activeShaderReplacementHash = 0;
		pipeline.activeShaderReplacementUsesFallback = false;
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
			{
				UnregisterKnownPipelineStateLocked(uncaptured.replacementPipelineState);
				uncaptured.replacementPipelineState->Release();
			}

			uncaptured.replacementPipelineState = nullptr;
			uncaptured.activeShaderReplacementName.clear();
			uncaptured.activeShaderReplacementType = ShaderReplacement::Unknown;
			uncaptured.activeShaderReplacementHash = 0;
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

			if (pipeline.psoWithReplacement && ReplacementStillEnabled(pipeline.activeShaderReplacementName, pipeline.activeShaderReplacementHash, pipeline.activeShaderReplacementType))
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

			if (pipeline.psoWithReplacement && ReplacementStillEnabled(pipeline.activeShaderReplacementName, pipeline.activeShaderReplacementHash, pipeline.activeShaderReplacementType))
				gPipelineStateOverrides[pipeline.pipelineState] = pipeline.psoWithReplacement;
		}

		for (auto& uncaptured : gUncapturedPipelineStates)
		{
			if (!uncaptured.pipelineState || !uncaptured.replacementPipelineState)
				continue;

			if (ReplacementStillEnabled(uncaptured.activeShaderReplacementName, uncaptured.activeShaderReplacementHash, uncaptured.activeShaderReplacementType))
				gPipelineStateOverrides[uncaptured.pipelineState] = uncaptured.replacementPipelineState;
		}

		gPipelineStateOverridesDirty = false;
	}


	bool GetRootSignatureBlob(ID3D12RootSignature* rootSignature, std::vector<uint8_t>& outBlob, uint64_t& outHash)
	{
		outBlob.clear();
		outHash = 0;

		if (!rootSignature)
			return false;

		auto it = gRootSignatureInfoByPointer.find(rootSignature);
		if (it == gRootSignatureInfoByPointer.end() || it->second.blob.empty())
			return false;

		outBlob = it->second.blob;
		outHash = it->second.hash;
		return outHash != 0;
	}

	ID3D12RootSignature* GetOrCreatePersistedRootSignature(const ShaderReplacement::ShaderReplacementDisk& replacement)
	{
		if (replacement.rootSignatureBlobPath.empty() || !gDevice)
			return nullptr;

		auto existingIt = gPersistedRootSignaturesByPath.find(replacement.rootSignatureBlobPath);
		if (existingIt != gPersistedRootSignaturesByPath.end())
			return existingIt->second;

		std::vector<uint8_t> blob;
		if (!ShaderInjectorIO::LoadDXILBlobFromDisk(replacement.rootSignatureBlobPath, blob))
		{
			ShaderInjectorGUI::WriteToRuntimeLog("GetOrCreatePersistedRootSignature: missing blob for " + replacement.name);
			return nullptr;
		}

		ID3D12RootSignature* rootSignature = nullptr;
		HRESULT hr = E_FAIL;
		if (Original_CreateRootSignature)
			hr = Original_CreateRootSignature(gDevice, 0, blob.data(), blob.size(), IID_PPV_ARGS(&rootSignature));
		else
			hr = gDevice->CreateRootSignature(0, blob.data(), blob.size(), IID_PPV_ARGS(&rootSignature));

		if (FAILED(hr) || !rootSignature)
		{
			ShaderInjectorGUI::WriteToRuntimeLog("GetOrCreatePersistedRootSignature: failed hr=" + std::to_string((unsigned)hr) + " replacement=" + replacement.name);
			return nullptr;
		}

		gPersistedRootSignaturesByPath[replacement.rootSignatureBlobPath] = rootSignature;
		return rootSignature;
	}

	bool LoadPersistedShaderBlob(const std::string& path, std::vector<uint8_t>& bytecode, uint64_t& hash, SIZE_T& size)
	{
		bytecode.clear();
		hash = 0;
		size = 0;

		if (path.empty())
			return true;

		if (!ShaderInjectorIO::LoadDXILBlobFromDisk(path, bytecode))
			return false;

		size = bytecode.size();
		hash = bytecode.empty() ? 0 : Hash::HashMemory(bytecode.data(), bytecode.size());
		return true;
	}

	bool LoadPersistedStreamTemplateFromReplacement(const ShaderReplacement::ShaderReplacementDisk& replacement, PipelineStateInfo& outPipeline);

	void BackfillReplacementPortableMetadataFromSidecars(ShaderReplacement::ShaderReplacementDisk& replacement)
	{
		if (!replacement.rootSignatureBlobPath.empty())
		{
			std::vector<uint8_t> rootBlob;
			if (ShaderInjectorIO::LoadDXILBlobFromDisk(replacement.rootSignatureBlobPath, rootBlob) && !rootBlob.empty())
			{
				replacement.rootSignatureLength = std::to_string(rootBlob.size());
				if (replacement.rootSignatureHash.empty())
					replacement.rootSignatureHash = Hash::FormatHash(Hash::HashMemory(rootBlob.data(), rootBlob.size()));
			}
		}

		if (replacement.pipelineStreamBlobPath.empty())
			return;

		PipelineStateInfo persistedPipeline{};
		if (!LoadPersistedStreamTemplateFromReplacement(replacement, persistedPipeline))
			return;

		FillStreamReplacementPortableStateFromBlob(replacement, persistedPipeline);
	}
	bool LoadPersistedStreamTemplateFromReplacement(const ShaderReplacement::ShaderReplacementDisk& replacement, PipelineStateInfo& outPipeline)
	{
		outPipeline = PipelineStateInfo{};

		if (replacement.pipelineStreamBlobPath.empty())
			return false;

		if (!ShaderInjectorIO::LoadDXILBlobFromDisk(replacement.pipelineStreamBlobPath, outPipeline.streamBlob))
		{
			ShaderInjectorGUI::WriteToRuntimeLog("LoadPersistedStreamTemplate: missing stream blob for " + replacement.name);
			return false;
		}

		if (!LoadPersistedShaderBlob(replacement.vertexShaderBlobPath, outPipeline.vsBytecode, outPipeline.vsHash, outPipeline.vsSize) ||
			!LoadPersistedShaderBlob(replacement.pixelShaderBlobPath, outPipeline.psBytecode, outPipeline.psHash, outPipeline.psSize) ||
			!LoadPersistedShaderBlob(replacement.computeShaderBlobPath, outPipeline.csBytecode, outPipeline.csHash, outPipeline.csSize) ||
			!LoadPersistedShaderBlob(replacement.geometryShaderBlobPath, outPipeline.gsBytecode, outPipeline.gsHash, outPipeline.gsSize) ||
			!LoadPersistedShaderBlob(replacement.hullShaderBlobPath, outPipeline.hsBytecode, outPipeline.hsHash, outPipeline.hsSize) ||
			!LoadPersistedShaderBlob(replacement.domainShaderBlobPath, outPipeline.dsBytecode, outPipeline.dsHash, outPipeline.dsSize))
		{
			ShaderInjectorGUI::WriteToRuntimeLog("LoadPersistedStreamTemplate: missing original shader stage blob for " + replacement.name);
			return false;
		}

		outPipeline.isCompute = !outPipeline.csBytecode.empty();
		outPipeline.isGraphics = !outPipeline.vsBytecode.empty() || !outPipeline.psBytecode.empty() || !outPipeline.gsBytecode.empty() || !outPipeline.hsBytecode.empty() || !outPipeline.dsBytecode.empty();

		if (!replacement.pipelineStreamMetadataPath.empty())
		{
			ShaderReplacement::ShaderPipelineStreamMetadataDisk metadata{};
			if (!ShaderReplacement::LoadPipelineStreamMetadataJson(replacement.pipelineStreamMetadataPath, metadata))
			{
				ShaderInjectorGUI::WriteToRuntimeLog("LoadPersistedStreamTemplate: missing stream metadata for " + replacement.name);
				return false;
			}

			ApplyPipelineStreamMetadata(metadata, outPipeline);
		}

		return true;
	}

	uint64_t StreamShaderHashForType(const PipelineStateInfo& pipeline, ShaderReplacement::ShaderType shaderType)
	{
		switch (shaderType)
		{
			case ShaderReplacement::VertexShader: return pipeline.vsHash;
			case ShaderReplacement::HullShader: return pipeline.hsHash;
			case ShaderReplacement::DomainShader: return pipeline.dsHash;
			case ShaderReplacement::GeometryShader: return pipeline.gsHash;
			case ShaderReplacement::PixelShader: return pipeline.psHash;
			case ShaderReplacement::ComputeShader: return pipeline.csHash;
			default: return 0;
		}
	}

	bool StreamPipelineHasShaderHash(const PipelineStateInfo& pipeline, ShaderReplacement::ShaderType shaderType, uint64_t shaderHash)
	{
		return shaderHash != 0 && StreamShaderHashForType(pipeline, shaderType) == shaderHash;
	}

	void FillPipelineTemplateCommonState(ShaderReplacement::ShaderPipelineTemplateDisk& pipelineTemplate, const PipelineStateInfo& pipeline)
	{
		pipelineTemplate.vsHash = pipeline.vsHash ? Hash::FormatHash(pipeline.vsHash) : "";
		pipelineTemplate.psHash = pipeline.psHash ? Hash::FormatHash(pipeline.psHash) : "";
		pipelineTemplate.csHash = pipeline.csHash ? Hash::FormatHash(pipeline.csHash) : "";
		pipelineTemplate.gsHash = pipeline.gsHash ? Hash::FormatHash(pipeline.gsHash) : "";
		pipelineTemplate.hsHash = pipeline.hsHash ? Hash::FormatHash(pipeline.hsHash) : "";
		pipelineTemplate.dsHash = pipeline.dsHash ? Hash::FormatHash(pipeline.dsHash) : "";
		pipelineTemplate.vsLength = pipeline.vsSize ? std::to_string((size_t)pipeline.vsSize) : "";
		pipelineTemplate.psLength = pipeline.psSize ? std::to_string((size_t)pipeline.psSize) : "";
		pipelineTemplate.csLength = pipeline.csSize ? std::to_string((size_t)pipeline.csSize) : "";
		pipelineTemplate.gsLength = pipeline.gsSize ? std::to_string((size_t)pipeline.gsSize) : "";
		pipelineTemplate.hsLength = pipeline.hsSize ? std::to_string((size_t)pipeline.hsSize) : "";
		pipelineTemplate.dsLength = pipeline.dsSize ? std::to_string((size_t)pipeline.dsSize) : "";
		pipelineTemplate.inputLayoutElementCount = std::to_string(pipeline.inputElements.size());
		pipelineTemplate.inputLayoutSignature = InputLayoutSignature(pipeline.inputElements);
		pipelineTemplate.streamOutputDeclarationCount = std::to_string(pipeline.soDeclarations.size());
		pipelineTemplate.streamOutputSignature = StreamOutputSignature(pipeline.soDeclarations, pipeline.soStrides);
		pipelineTemplate.pipelineStreamLength = pipeline.streamBlob.empty() ? "" : std::to_string(pipeline.streamBlob.size());
		pipelineTemplate.pipelineStreamSubobjectTypes = PipelineStreamSubobjectTypeSignature(pipeline.streamBlob);
	}

	ShaderReplacement::ShaderReplacementDisk ReplacementWithPipelineTemplate(const ShaderReplacement::ShaderReplacementDisk& replacement, const ShaderReplacement::ShaderPipelineTemplateDisk& pipelineTemplate)
	{
		ShaderReplacement::ShaderReplacementDisk templateReplacement = replacement;
		templateReplacement.name = replacement.name + "/" + pipelineTemplate.name;
		templateReplacement.pipelineIndex = pipelineTemplate.pipelineIndex;
		templateReplacement.psoPointer = pipelineTemplate.psoPointer;
		templateReplacement.pipelineCachedBlobHash = pipelineTemplate.pipelineCachedBlobHash;
		templateReplacement.pipelineCachedBlobLength = pipelineTemplate.pipelineCachedBlobLength;
		templateReplacement.pipelineCachedBlobPath = pipelineTemplate.pipelineCachedBlobPath;
		templateReplacement.pipelineStreamBlobPath = pipelineTemplate.pipelineStreamBlobPath;
		templateReplacement.pipelineStreamMetadataPath = pipelineTemplate.pipelineStreamMetadataPath;
		templateReplacement.rootSignatureBlobPath = pipelineTemplate.rootSignatureBlobPath;
		templateReplacement.rootSignatureHash = pipelineTemplate.rootSignatureHash;
		templateReplacement.rootSignatureLength = pipelineTemplate.rootSignatureLength;
		templateReplacement.vertexShaderBlobPath = pipelineTemplate.vertexShaderBlobPath;
		templateReplacement.pixelShaderBlobPath = pipelineTemplate.pixelShaderBlobPath;
		templateReplacement.computeShaderBlobPath = pipelineTemplate.computeShaderBlobPath;
		templateReplacement.geometryShaderBlobPath = pipelineTemplate.geometryShaderBlobPath;
		templateReplacement.hullShaderBlobPath = pipelineTemplate.hullShaderBlobPath;
		templateReplacement.domainShaderBlobPath = pipelineTemplate.domainShaderBlobPath;
		templateReplacement.vsHash = pipelineTemplate.vsHash;
		templateReplacement.psHash = pipelineTemplate.psHash;
		templateReplacement.csHash = pipelineTemplate.csHash;
		templateReplacement.gsHash = pipelineTemplate.gsHash;
		templateReplacement.hsHash = pipelineTemplate.hsHash;
		templateReplacement.dsHash = pipelineTemplate.dsHash;
		templateReplacement.vsLength = pipelineTemplate.vsLength;
		templateReplacement.psLength = pipelineTemplate.psLength;
		templateReplacement.csLength = pipelineTemplate.csLength;
		templateReplacement.gsLength = pipelineTemplate.gsLength;
		templateReplacement.hsLength = pipelineTemplate.hsLength;
		templateReplacement.dsLength = pipelineTemplate.dsLength;
		templateReplacement.pipelineStreamLength = pipelineTemplate.pipelineStreamLength;
		templateReplacement.pipelineStreamSubobjectTypes = pipelineTemplate.pipelineStreamSubobjectTypes;
		templateReplacement.inputLayoutElementCount = pipelineTemplate.inputLayoutElementCount;
		templateReplacement.inputLayoutSignature = pipelineTemplate.inputLayoutSignature;
		templateReplacement.streamOutputDeclarationCount = pipelineTemplate.streamOutputDeclarationCount;
		templateReplacement.streamOutputSignature = pipelineTemplate.streamOutputSignature;
		return templateReplacement;
	}

	SIZE_T CountMatchingBytes(const std::vector<uint8_t>& lhs, const std::vector<uint8_t>& rhs)
	{
		const SIZE_T count = min(lhs.size(), rhs.size());
		SIZE_T matches = 0;
		for (SIZE_T i = 0; i < count; ++i)
		{
			if (lhs[i] == rhs[i])
				matches++;
		}
		return matches;
	}

	bool WriteStreamPipelineTemplateVariant(ShaderReplacement::ShaderReplacementDisk& replacement, const PipelineStateInfo& pipeline, int pipelineIndex, int templateIndex, bool& ok)
	{
		if (pipeline.streamBlob.empty())
			return false;

		char prefix[64]{};
		sprintf_s(prefix, "PipelineTemplate_%03d", templateIndex);

		ShaderReplacement::ShaderPipelineTemplateDisk pipelineTemplate{};
		pipelineTemplate.name = prefix;
		pipelineTemplate.sourceList = "Stream";
		pipelineTemplate.pipelineIndex = std::to_string(pipelineIndex);
		pipelineTemplate.psoPointer = PointerToString(pipeline.pipelineState);
		pipelineTemplate.pipelineStreamBlobPath = replacement.replacementDirectory + "\\" + prefix + "_PipelineStateStream" + ShaderInjectorIO::extensionBIN;
		pipelineTemplate.pipelineStreamMetadataPath = replacement.replacementDirectory + "\\" + prefix + "_PipelineStateStreamMetadata" + ShaderInjectorIO::extensionJSON;
		FillPipelineTemplateCommonState(pipelineTemplate, pipeline);

		uint64_t cachedBlobHash = 0;
		SIZE_T cachedBlobSize = 0;
		std::vector<uint8_t> cachedBlob;
		if (GetPipelineCachedBlobInfo(pipeline.pipelineState, cachedBlobHash, cachedBlobSize, &cachedBlob))
		{
			pipelineTemplate.pipelineCachedBlobHash = Hash::FormatHash(cachedBlobHash);
			pipelineTemplate.pipelineCachedBlobLength = std::to_string(cachedBlobSize);
			pipelineTemplate.pipelineCachedBlobPath = replacement.replacementDirectory + "\\" + prefix + "_OriginalPipelineCachedBlob" + ShaderInjectorIO::extensionBIN;
		}

		std::vector<uint8_t> rootSignatureBlob;
		uint64_t rootSignatureHash = 0;
		if (GetRootSignatureBlob(pipeline.rootSignature, rootSignatureBlob, rootSignatureHash))
		{
			pipelineTemplate.rootSignatureHash = Hash::FormatHash(rootSignatureHash);
			pipelineTemplate.rootSignatureLength = std::to_string(rootSignatureBlob.size());
			pipelineTemplate.rootSignatureBlobPath = replacement.replacementDirectory + "\\" + prefix + "_RootSignatureBlob" + ShaderInjectorIO::extensionBIN;
		}

		if (!pipeline.vsBytecode.empty()) pipelineTemplate.vertexShaderBlobPath = replacement.replacementDirectory + "\\" + prefix + "_OriginalVertexShaderBytecode" + ShaderInjectorIO::extensionBIN;
		if (!pipeline.psBytecode.empty()) pipelineTemplate.pixelShaderBlobPath = replacement.replacementDirectory + "\\" + prefix + "_OriginalPixelShaderBytecode" + ShaderInjectorIO::extensionBIN;
		if (!pipeline.csBytecode.empty()) pipelineTemplate.computeShaderBlobPath = replacement.replacementDirectory + "\\" + prefix + "_OriginalComputeShaderBytecode" + ShaderInjectorIO::extensionBIN;
		if (!pipeline.gsBytecode.empty()) pipelineTemplate.geometryShaderBlobPath = replacement.replacementDirectory + "\\" + prefix + "_OriginalGeometryShaderBytecode" + ShaderInjectorIO::extensionBIN;
		if (!pipeline.hsBytecode.empty()) pipelineTemplate.hullShaderBlobPath = replacement.replacementDirectory + "\\" + prefix + "_OriginalHullShaderBytecode" + ShaderInjectorIO::extensionBIN;
		if (!pipeline.dsBytecode.empty()) pipelineTemplate.domainShaderBlobPath = replacement.replacementDirectory + "\\" + prefix + "_OriginalDomainShaderBytecode" + ShaderInjectorIO::extensionBIN;

		ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.pipelineStreamBlobPath, pipeline.streamBlob.data(), pipeline.streamBlob.size()) && ok;
		ShaderReplacement::ShaderPipelineStreamMetadataDisk metadata = BuildPipelineStreamMetadata(pipeline);
		ok = ShaderReplacement::WritePipelineStreamMetadataJson(pipelineTemplate.pipelineStreamMetadataPath, metadata) && ok;
		if (!cachedBlob.empty() && !pipelineTemplate.pipelineCachedBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.pipelineCachedBlobPath, cachedBlob.data(), cachedBlob.size()) && ok;
		if (!rootSignatureBlob.empty() && !pipelineTemplate.rootSignatureBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.rootSignatureBlobPath, rootSignatureBlob.data(), rootSignatureBlob.size()) && ok;
		if (!pipeline.vsBytecode.empty() && !pipelineTemplate.vertexShaderBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.vertexShaderBlobPath, pipeline.vsBytecode.data(), pipeline.vsBytecode.size()) && ok;
		if (!pipeline.psBytecode.empty() && !pipelineTemplate.pixelShaderBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.pixelShaderBlobPath, pipeline.psBytecode.data(), pipeline.psBytecode.size()) && ok;
		if (!pipeline.csBytecode.empty() && !pipelineTemplate.computeShaderBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.computeShaderBlobPath, pipeline.csBytecode.data(), pipeline.csBytecode.size()) && ok;
		if (!pipeline.gsBytecode.empty() && !pipelineTemplate.geometryShaderBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.geometryShaderBlobPath, pipeline.gsBytecode.data(), pipeline.gsBytecode.size()) && ok;
		if (!pipeline.hsBytecode.empty() && !pipelineTemplate.hullShaderBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.hullShaderBlobPath, pipeline.hsBytecode.data(), pipeline.hsBytecode.size()) && ok;
		if (!pipeline.dsBytecode.empty() && !pipelineTemplate.domainShaderBlobPath.empty()) ok = ShaderInjectorIO::WriteBinaryFile(pipelineTemplate.domainShaderBlobPath, pipeline.dsBytecode.data(), pipeline.dsBytecode.size()) && ok;

		replacement.pipelineTemplates.push_back(pipelineTemplate);
		return true;
	}

	void WriteMatchingStreamPipelineTemplateVariants(ShaderReplacement::ShaderReplacementDisk& replacement, ShaderReplacement::ShaderType shaderType, uint64_t shaderHash, bool& ok)
	{
		if (shaderHash == 0)
			return;

		int templateIndex = 0;
		for (int i = 0; i < (int)gPipelineStates.size(); ++i)
		{
			const PipelineStateInfo& pipeline = gPipelineStates[i];
			if (!StreamPipelineHasShaderHash(pipeline, shaderType, shaderHash))
				continue;

			WriteStreamPipelineTemplateVariant(replacement, pipeline, i, templateIndex++, ok);
		}

		if (templateIndex > 1)
			ShaderInjectorGUI::WriteToRuntimeLog("Captured stream pipeline template variants for " + replacement.name + ": " + std::to_string(templateIndex));
	}
	bool SelectPersistedPipelineTemplateForUncaptured(
		const ShaderReplacement::ShaderReplacementDisk& replacement,
		const UncapturedPipelineStateInfo& uncaptured,
		ShaderReplacement::ShaderReplacementDisk& outTemplateReplacement,
		std::string& outTemplateName,
		SIZE_T& outMatchingBytes)
	{
		outTemplateReplacement = replacement;
		outTemplateName.clear();
		outMatchingBytes = 0;

		if (replacement.pipelineTemplates.empty())
			return true;

		int bestIndex = -1;
		SIZE_T bestMatchingBytes = 0;
		int exactHashIndex = -1;

		for (int i = 0; i < (int)replacement.pipelineTemplates.size(); ++i)
		{
			const ShaderReplacement::ShaderPipelineTemplateDisk& pipelineTemplate = replacement.pipelineTemplates[i];
			const uint64_t templateHash = Hash::ParseHashText(pipelineTemplate.pipelineCachedBlobHash);
			if (templateHash != 0 && templateHash == uncaptured.cachedBlobHash)
			{
				exactHashIndex = i;
				break;
			}

			if (pipelineTemplate.pipelineCachedBlobLength.empty() || uncaptured.cachedBlobSize == 0)
				continue;

			const SIZE_T templateSize = (SIZE_T)_strtoui64(pipelineTemplate.pipelineCachedBlobLength.c_str(), nullptr, 10);
			if (templateSize != uncaptured.cachedBlobSize)
				continue;

			SIZE_T matchingBytes = 1;
			if (!uncaptured.cachedBlob.empty() && !pipelineTemplate.pipelineCachedBlobPath.empty())
			{
				std::vector<uint8_t> templateCachedBlob;
				if (ShaderInjectorIO::LoadDXILBlobFromDisk(pipelineTemplate.pipelineCachedBlobPath, templateCachedBlob) && templateCachedBlob.size() == uncaptured.cachedBlob.size())
					matchingBytes = CountMatchingBytes(templateCachedBlob, uncaptured.cachedBlob);
			}

			if (matchingBytes > bestMatchingBytes)
			{
				bestMatchingBytes = matchingBytes;
				bestIndex = i;
			}
		}

		const int selectedIndex = exactHashIndex >= 0 ? exactHashIndex : bestIndex;
		if (selectedIndex < 0)
			return true;

		const ShaderReplacement::ShaderPipelineTemplateDisk& selectedTemplate = replacement.pipelineTemplates[selectedIndex];
		outTemplateReplacement = ReplacementWithPipelineTemplate(replacement, selectedTemplate);
		outTemplateName = selectedTemplate.name;
		outMatchingBytes = exactHashIndex >= 0 ? uncaptured.cachedBlobSize : bestMatchingBytes;
		return true;
	}
	bool CreateReplacementShaderTemplate(
		const std::string& sourceList,
		int pipelineIndex,
		ShaderReplacement::ShaderType shaderType,
		uint64_t shaderHash,
		size_t shaderBytecodeLength,
		const void* shaderBytecode,
		ID3D12PipelineState* pipelineState,
		const GraphicsPipelineInfo* graphicsInfo,
		const PipelineStateInfo* streamInfo)
	{
		if (!shaderHash || !shaderBytecode || shaderBytecodeLength == 0)
		{
			ShaderInjectorGUI::WriteToRuntimeLog("CreateReplacementShaderTemplate: missing shader bytecode");
			return false;
		}

		const std::string hashText = Hash::FormatHash(shaderHash);
		const std::string shaderTypeText = ShaderReplacement::ShaderTypeToString(shaderType);
		const std::string replacementName = "ShaderReplacement_" + shaderTypeText + "_" + hashText;
		const std::string replacementDirectory = ShaderInjectorIO::GetShaderReplacementsDirectory() + "\\" + replacementName;

		if (!ShaderInjectorIO::DirectoryExists(replacementDirectory))
			ShaderInjectorIO::DirectoryCreate(replacementDirectory);

		if (!ShaderInjectorIO::DirectoryExists(replacementDirectory))
		{
			ShaderInjectorGUI::WriteToRuntimeLog("CreateReplacementShaderTemplate: failed to create directory " + replacementDirectory);
			return false;
		}

		ShaderReplacement::ShaderReplacementDisk replacement{};
		replacement.schemaVersion = 1;
		replacement.enabled = true;
		replacement.name = replacementName;
		replacement.shaderType = shaderType;
		replacement.shaderProfile = ShaderProfileForType(shaderType);
		replacement.shaderEntryPoint = "main";
		replacement.originalShaderBytecodeHash = hashText;
		replacement.originalShaderBytecodeLength = std::to_string(shaderBytecodeLength);
		replacement.replacementDirectory = replacementDirectory;
		replacement.originalShaderBlobPath = replacementDirectory + "\\OriginalShaderBytecode" + ShaderInjectorIO::extensionBIN;
		replacement.modifiedShaderBlobPath = replacementDirectory + "\\NewCompiledBlob" + ShaderInjectorIO::extensionBLOB;
		replacement.jsonPath = replacementDirectory + "\\ShaderReplacement" + ShaderInjectorIO::extensionJSON;
		replacement.sourceList = sourceList;
		replacement.pipelineIndex = std::to_string(pipelineIndex);
		replacement.pipelineStateType = sourceList == "Stream" ? "PipelineStateStream" : "GraphicsPipelineStateDesc";
		replacement.psoPointer = PointerToString(pipelineState);

		uint64_t cachedBlobHash = 0;
		SIZE_T cachedBlobSize = 0;
		std::vector<uint8_t> cachedBlob;
		if (GetPipelineCachedBlobInfo(pipelineState, cachedBlobHash, cachedBlobSize, &cachedBlob))
		{
			replacement.pipelineCachedBlobHash = Hash::FormatHash(cachedBlobHash);
			replacement.pipelineCachedBlobLength = std::to_string(cachedBlobSize);
			replacement.pipelineCachedBlobPath = replacement.replacementDirectory + "\\OriginalPipelineCachedBlob" + ShaderInjectorIO::extensionBIN;
		}

		replacement.targetSubobjectType = std::to_string((UINT)SubobjectTypeForShaderType(shaderType));
		replacement.notes = "Generated by Shader Injector. Sidecar paths are stored as filenames and resolved relative to this JSON file at runtime. Shader source resolves from ShaderSources/<shader type>s.";

		if (graphicsInfo)
		{
			FillCommonReplacementHashes(replacement, graphicsInfo->vsHash, graphicsInfo->psHash, 0, graphicsInfo->gsHash, graphicsInfo->hsHash, graphicsInfo->dsHash);
			FillGraphicsReplacementPortableState(replacement, *graphicsInfo);

			std::vector<uint8_t> rootSignatureBlob;
			uint64_t rootSignatureHash = 0;
			if (GetRootSignatureBlob(graphicsInfo->originalDesc.pRootSignature, rootSignatureBlob, rootSignatureHash))
			{
				replacement.rootSignatureHash = Hash::FormatHash(rootSignatureHash);
				replacement.rootSignatureLength = std::to_string(rootSignatureBlob.size());
			}
		}
		else if (streamInfo)
		{
			FillCommonReplacementHashes(replacement, streamInfo->vsHash, streamInfo->psHash, streamInfo->csHash, streamInfo->gsHash, streamInfo->hsHash, streamInfo->dsHash);
			FillStreamReplacementPortableStateFromBlob(replacement, *streamInfo);

			if (!streamInfo->streamBlob.empty())
				replacement.pipelineStreamBlobPath = replacementDirectory + "\\PipelineStateStream" + ShaderInjectorIO::extensionBIN;
			if (!streamInfo->streamBlob.empty())
				replacement.pipelineStreamMetadataPath = replacementDirectory + "\\PipelineStateStreamMetadata" + ShaderInjectorIO::extensionJSON;
			std::vector<uint8_t> rootSignatureBlob;
			uint64_t rootSignatureHash = 0;
			if (GetRootSignatureBlob(streamInfo->rootSignature, rootSignatureBlob, rootSignatureHash))
			{
				replacement.rootSignatureHash = Hash::FormatHash(rootSignatureHash);
				replacement.rootSignatureLength = std::to_string(rootSignatureBlob.size());
				replacement.rootSignatureBlobPath = replacementDirectory + "\\RootSignatureBlob" + ShaderInjectorIO::extensionBIN;
			}
			if (!streamInfo->vsBytecode.empty())
				replacement.vertexShaderBlobPath = replacementDirectory + "\\OriginalVertexShaderBytecode" + ShaderInjectorIO::extensionBIN;
			if (!streamInfo->psBytecode.empty())
				replacement.pixelShaderBlobPath = replacementDirectory + "\\OriginalPixelShaderBytecode" + ShaderInjectorIO::extensionBIN;
			if (!streamInfo->csBytecode.empty())
				replacement.computeShaderBlobPath = replacementDirectory + "\\OriginalComputeShaderBytecode" + ShaderInjectorIO::extensionBIN;
			if (!streamInfo->gsBytecode.empty())
				replacement.geometryShaderBlobPath = replacementDirectory + "\\OriginalGeometryShaderBytecode" + ShaderInjectorIO::extensionBIN;
			if (!streamInfo->hsBytecode.empty())
				replacement.hullShaderBlobPath = replacementDirectory + "\\OriginalHullShaderBytecode" + ShaderInjectorIO::extensionBIN;
			if (!streamInfo->dsBytecode.empty())
				replacement.domainShaderBlobPath = replacementDirectory + "\\OriginalDomainShaderBytecode" + ShaderInjectorIO::extensionBIN;
		}

		bool ok = true;
		ok = ShaderInjectorIO::WriteBinaryFile(replacement.originalShaderBlobPath, shaderBytecode, shaderBytecodeLength) && ok;
		if (!cachedBlob.empty() && !replacement.pipelineCachedBlobPath.empty())
			ok = ShaderInjectorIO::WriteBinaryFile(replacement.pipelineCachedBlobPath, cachedBlob.data(), cachedBlob.size()) && ok;
		if (streamInfo && !streamInfo->streamBlob.empty() && !replacement.pipelineStreamBlobPath.empty())
			ok = ShaderInjectorIO::WriteBinaryFile(replacement.pipelineStreamBlobPath, streamInfo->streamBlob.data(), streamInfo->streamBlob.size()) && ok;
		if (streamInfo && !replacement.pipelineStreamMetadataPath.empty())
		{
			ShaderReplacement::ShaderPipelineStreamMetadataDisk metadata = BuildPipelineStreamMetadata(*streamInfo);
			ok = ShaderReplacement::WritePipelineStreamMetadataJson(replacement.pipelineStreamMetadataPath, metadata) && ok;
		}
		if (streamInfo && !replacement.rootSignatureBlobPath.empty())
		{
			std::vector<uint8_t> rootSignatureBlob;
			uint64_t rootSignatureHash = 0;
			if (GetRootSignatureBlob(streamInfo->rootSignature, rootSignatureBlob, rootSignatureHash))
				ok = ShaderInjectorIO::WriteBinaryFile(replacement.rootSignatureBlobPath, rootSignatureBlob.data(), rootSignatureBlob.size()) && ok;
		}
		if (streamInfo && !streamInfo->vsBytecode.empty() && !replacement.vertexShaderBlobPath.empty())
			ok = ShaderInjectorIO::WriteBinaryFile(replacement.vertexShaderBlobPath, streamInfo->vsBytecode.data(), streamInfo->vsBytecode.size()) && ok;
		if (streamInfo && !streamInfo->psBytecode.empty() && !replacement.pixelShaderBlobPath.empty())
			ok = ShaderInjectorIO::WriteBinaryFile(replacement.pixelShaderBlobPath, streamInfo->psBytecode.data(), streamInfo->psBytecode.size()) && ok;
		if (streamInfo && !streamInfo->csBytecode.empty() && !replacement.computeShaderBlobPath.empty())
			ok = ShaderInjectorIO::WriteBinaryFile(replacement.computeShaderBlobPath, streamInfo->csBytecode.data(), streamInfo->csBytecode.size()) && ok;
		if (streamInfo && !streamInfo->gsBytecode.empty() && !replacement.geometryShaderBlobPath.empty())
			ok = ShaderInjectorIO::WriteBinaryFile(replacement.geometryShaderBlobPath, streamInfo->gsBytecode.data(), streamInfo->gsBytecode.size()) && ok;
		if (streamInfo && !streamInfo->hsBytecode.empty() && !replacement.hullShaderBlobPath.empty())
			ok = ShaderInjectorIO::WriteBinaryFile(replacement.hullShaderBlobPath, streamInfo->hsBytecode.data(), streamInfo->hsBytecode.size()) && ok;
		if (streamInfo && !streamInfo->dsBytecode.empty() && !replacement.domainShaderBlobPath.empty())
			ok = ShaderInjectorIO::WriteBinaryFile(replacement.domainShaderBlobPath, streamInfo->dsBytecode.data(), streamInfo->dsBytecode.size()) && ok;

		if (streamInfo)
			WriteMatchingStreamPipelineTemplateVariants(replacement, shaderType, shaderHash, ok);

		ok = ShaderInjectorIO::GenerateShaderTextDXIL(replacement.originalShaderBlobPath) && ok;
		ok = WriteShaderReplacementJson(replacement) && ok;

		if (!ok)
		{
			ShaderInjectorGUI::WriteToRuntimeLog("CreateReplacementShaderTemplate: failed to write one or more files for " + replacementName);
			return false;
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Created replacement shader template: " + replacement.jsonPath);
		gLoadedShaderReplacementsOnce = false;
		MarkShaderReplacementApplyDirty();
		return true;
	}

	bool CreateReplacementShaderTemplateForPipeline(
		const std::string& sourceList,
		int pipelineIndex,
		ShaderReplacement::ShaderType shaderType,
		uint64_t shaderHash,
		size_t shaderBytecodeLength,
		const void* shaderBytecode,
		GraphicsPipelineInfo& pipeline)
	{
		return CreateReplacementShaderTemplate(
			sourceList,
			pipelineIndex,
			shaderType,
			shaderHash,
			shaderBytecodeLength,
			shaderBytecode,
			pipeline.pipelineState,
			&pipeline,
			nullptr);
	}

	bool CreateReplacementShaderTemplateForPipeline(
		const std::string& sourceList,
		int pipelineIndex,
		ShaderReplacement::ShaderType shaderType,
		uint64_t shaderHash,
		size_t shaderBytecodeLength,
		const void* shaderBytecode,
		PipelineStateInfo& pipeline)
	{
		return CreateReplacementShaderTemplate(
			sourceList,
			pipelineIndex,
			shaderType,
			shaderHash,
			shaderBytecodeLength,
			shaderBytecode,
			pipeline.pipelineState,
			nullptr,
			&pipeline);
	}

	void RefreshLoadedShaderReplacements()
	{
		gLoadedShaderReplacements.clear();
		gLoadedShaderReplacementBlobs.clear();
		gSelectedShaderReplacementIndex = -1;
		gShaderReplacementNameBufferIndex = -1;
		gShaderReplacementNameBuffer[0] = '\0';

		const std::string replacementDirectory = ShaderInjectorIO::GetShaderReplacementsDirectory();

		if (!ShaderInjectorIO::DirectoryExists(replacementDirectory))
			ShaderInjectorIO::DirectoryCreate(replacementDirectory);

		std::vector<std::string> jsonFiles;
		ShaderReplacement::CollectShaderReplacementJsonFiles(replacementDirectory, jsonFiles);
		std::sort(jsonFiles.begin(), jsonFiles.end());

		for (const std::string& jsonPath : jsonFiles)
		{
			ShaderReplacement::ShaderReplacementDisk replacement{};

			if (LoadShaderReplacementJson(jsonPath, replacement))
			{
				BackfillReplacementPortableMetadataFromSidecars(replacement);
				std::vector<uint8_t> modifiedBlob;

				if (!replacement.modifiedShaderBlobPath.empty() && ShaderInjectorIO::FileExists(replacement.modifiedShaderBlobPath))
					ShaderInjectorIO::LoadDXILBlobFromDisk(replacement.modifiedShaderBlobPath, modifiedBlob);

				gLoadedShaderReplacements.push_back(replacement);
				gLoadedShaderReplacementBlobs.push_back(modifiedBlob);
			}
		}

		if (!gLoadedShaderReplacements.empty())
			gSelectedShaderReplacementIndex = 0;

		for (auto& uncaptured : gUncapturedPipelineStates)
			uncaptured.attemptedReplacement = false;

		gLoadedShaderReplacementsOnce = true;
		MarkShaderReplacementApplyDirty();
		ShaderInjectorGUI::WriteToRuntimeLog("Loaded shader replacements from disk: " + std::to_string(gLoadedShaderReplacements.size()));

		for (const auto& replacement : gLoadedShaderReplacements)
		{
			ShaderInjectorGUI::WriteToRuntimeLog(
				"Replacement loaded: " + replacement.name +
				" enabled=" + std::to_string(replacement.enabled ? 1 : 0) +
				" shaderHash=" + replacement.originalShaderBytecodeHash +
				" cacheHash=" + replacement.pipelineCachedBlobHash +
				" cacheBytes=" + replacement.pipelineCachedBlobLength +
				" source=" + replacement.sourceList +
				" index=" + replacement.pipelineIndex);
		}
	}

	void SyncShaderReplacementNameBuffer()
	{
		if (gSelectedShaderReplacementIndex < 0 || gSelectedShaderReplacementIndex >= (int)gLoadedShaderReplacements.size())
		{
			gShaderReplacementNameBufferIndex = -1;
			gShaderReplacementNameBuffer[0] = '\0';
			return;
		}

		if (gShaderReplacementNameBufferIndex == gSelectedShaderReplacementIndex)
			return;

		const std::string& name = gLoadedShaderReplacements[gSelectedShaderReplacementIndex].name;
		strncpy_s(gShaderReplacementNameBuffer, name.c_str(), _TRUNCATE);
		gShaderReplacementNameBufferIndex = gSelectedShaderReplacementIndex;
	}

	bool SaveShaderReplacement(int index)
	{
		if (index < 0 || index >= (int)gLoadedShaderReplacements.size())
			return false;

		ShaderReplacement::ShaderReplacementDisk& replacement = gLoadedShaderReplacements[index];
		replacement.name = gShaderReplacementNameBuffer;

		if (!WriteShaderReplacementJson(replacement))
		{
			ShaderInjectorGUI::WriteToRuntimeLog("SaveShaderReplacement: failed to save " + replacement.jsonPath);
			return false;
		}

		MarkShaderReplacementApplyDirty();
		ShaderInjectorGUI::WriteToRuntimeLog("Saved shader replacement: " + replacement.jsonPath);
		return true;
	}

	bool CompileShaderReplacement(int index)
	{
		if (index < 0 || index >= (int)gLoadedShaderReplacements.size())
			return false;

		ShaderReplacement::ShaderReplacementDisk& replacement = gLoadedShaderReplacements[index];

		if (!replacement.shaderSourceName.empty())
			replacement.shaderSourcePath = ShaderInjectorIO::GetShaderSourcesDirectory(ShaderReplacement::ShaderTypeToString(replacement.shaderType) + "s") + "\\" + replacement.shaderSourceName;

		if (replacement.shaderSourcePath.empty() || !ShaderInjectorIO::FileExists(replacement.shaderSourcePath))
		{
			ShaderInjectorGUI::WriteToRuntimeLog("CompileShaderReplacement: source file not found " + replacement.shaderSourcePath);
			return false;
		}

		if (replacement.shaderProfile.empty() || replacement.shaderEntryPoint.empty())
		{
			ShaderInjectorGUI::WriteToRuntimeLog("CompileShaderReplacement: shader profile or entry point missing for " + replacement.name);
			return false;
		}

		std::string compiledBlobPath = replacement.modifiedShaderBlobPath;

		if (!ShaderInjectorIO::CompileSourceToDXILBlob(replacement.shaderSourcePath, replacement.shaderProfile, replacement.shaderEntryPoint, compiledBlobPath))
		{
			ShaderInjectorGUI::WriteToRuntimeLog("CompileShaderReplacement: compile failed for " + replacement.shaderSourcePath);
			return false;
		}

		replacement.modifiedShaderBlobPath = compiledBlobPath;
		WriteShaderReplacementJson(replacement);
		ShaderInjectorGUI::WriteToRuntimeLog("Compiled shader replacement: " + replacement.name + " -> " + replacement.modifiedShaderBlobPath);
		return true;
	}

	bool ReloadShaderReplacement(int index)
	{
		if (index < 0 || index >= (int)gLoadedShaderReplacements.size())
			return false;

		ShaderReplacement::ShaderReplacementDisk& replacement = gLoadedShaderReplacements[index];

		if (index >= (int)gLoadedShaderReplacementBlobs.size())
			gLoadedShaderReplacementBlobs.resize(gLoadedShaderReplacements.size());

		std::vector<uint8_t> loadedBlob;
		if (replacement.modifiedShaderBlobPath.empty() || !ShaderInjectorIO::LoadDXILBlobFromDisk(replacement.modifiedShaderBlobPath, loadedBlob) || loadedBlob.empty())
		{
			gLoadedShaderReplacementBlobs[index].clear();
			ShaderInjectorGUI::WriteToRuntimeLog("ReloadShaderReplacement: failed to load compiled blob " + replacement.modifiedShaderBlobPath);
		}
		else
		{
			gLoadedShaderReplacementBlobs[index] = loadedBlob;
			ShaderInjectorGUI::WriteToRuntimeLog("Reloaded shader replacement: " + replacement.name + " bytes=" + std::to_string(gLoadedShaderReplacementBlobs[index].size()));
		}

		{
			std::lock_guard<std::mutex> lock(gPipelineMutex);
			InvalidateAllReplacementPSOs();
		}

		MarkShaderReplacementApplyDirty();
		return !gLoadedShaderReplacementBlobs[index].empty();
	}

	bool DeleteShaderReplacement(int index)
	{
		if (index < 0 || index >= (int)gLoadedShaderReplacements.size())
			return false;

		ShaderReplacement::ShaderReplacementDisk replacement = gLoadedShaderReplacements[index];

		ShaderInjectorIO::DeleteFileIfExists(replacement.modifiedShaderBlobPath);
		ShaderInjectorIO::DeleteFileIfExists(replacement.originalShaderBlobPath);
		ShaderInjectorIO::DeleteFileIfExists(replacement.jsonPath);

		if (!replacement.replacementDirectory.empty() && ShaderInjectorIO::DirectoryExists(replacement.replacementDirectory))
			RemoveDirectoryA(replacement.replacementDirectory.c_str());

		{
			std::lock_guard<std::mutex> lock(gPipelineMutex);
			InvalidateAllReplacementPSOs();
		}

		ShaderInjectorGUI::WriteToRuntimeLog("Deleted shader replacement: " + replacement.name);
		RefreshLoadedShaderReplacements();
		MarkShaderReplacementApplyDirty();
		return true;
	}

	void GatherPipelineInfo(IDXGISwapChain3* swapChain)
	{
		if (!gDevice)
			return;

		ShaderInjectorIO::WriteToLogFile("[d3d12hook] gathering pipeline info...");

		//=========================================== Swapchain ===========================================
		DXGI_SWAP_CHAIN_DESC desc{};
		swapChain->GetDesc(&desc);

		gPipelineInfo.swapChainBuffers = desc.BufferCount;
		gPipelineInfo.swapChainFormat = desc.BufferDesc.Format;

		ShaderInjectorIO::WriteToLogFile("[d3d12hook] swapChainBuffers: " + std::to_string(gPipelineInfo.swapChainBuffers));
		ShaderInjectorIO::WriteToLogFile("[d3d12hook] swapChainFormat: " + std::to_string(gPipelineInfo.swapChainFormat));

		//=========================================== Adapter ===========================================
		IDXGIDevice* dxgiDevice = nullptr;

		//IMPORTANT NOTE: this often fails... but that is ok because we have a backup later
		if (SUCCEEDED(gDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice))))
		{
			IDXGIAdapter* adapter = nullptr;

			if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter)))
			{
				DXGI_ADAPTER_DESC adapterDesc{};
				adapter->GetDesc(&adapterDesc);

				char gpuName[256]{};
				wcstombs_s(nullptr, gpuName, adapterDesc.Description, sizeof(gpuName));

				gPipelineInfo.gpuName = gpuName;
				gPipelineInfo.vendorId = adapterDesc.VendorId;
				gPipelineInfo.deviceId = adapterDesc.DeviceId;
				gPipelineInfo.dedicatedVideoMemory = adapterDesc.DedicatedVideoMemory;
				gPipelineInfo.dedicatedSystemMemory = adapterDesc.DedicatedSystemMemory;
				gPipelineInfo.sharedSystemMemory = adapterDesc.SharedSystemMemory;

				adapter->Release();
			}

			dxgiDevice->Release();
		}
		else
		{
			IDXGIFactory6* factory = nullptr;
			HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));

			if (FAILED(hr))
			{
				gPipelineInfo.gpuName = "CreateDXGIFactory1 failed";
				return;
			}

			IDXGIAdapter1* adapter = nullptr;

			for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
			{
				DXGI_ADAPTER_DESC1 desc;
				adapter->GetDesc1(&desc);

				if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				{
					adapter->Release();
					continue;
				}

				char gpuName[256];
				wcstombs_s(nullptr, gpuName, sizeof(gpuName), desc.Description, _TRUNCATE);

				gPipelineInfo.gpuName = gpuName;
				gPipelineInfo.vendorId = desc.VendorId;
				gPipelineInfo.deviceId = desc.DeviceId;
				gPipelineInfo.dedicatedVideoMemory = desc.DedicatedVideoMemory;
				gPipelineInfo.dedicatedSystemMemory = desc.DedicatedSystemMemory;
				gPipelineInfo.sharedSystemMemory = desc.SharedSystemMemory;

				adapter->Release();
				break;
			}

			factory->Release();
		}

		//=========================================== D3D12 Feature Support ===========================================
		D3D12_FEATURE_DATA_D3D12_OPTIONS options{};

		if (SUCCEEDED(gDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
		{
			gPipelineInfo.resourceBindingTier = options.ResourceBindingTier;
			gPipelineInfo.tiledResourcesTier = options.TiledResourcesTier;
			gPipelineInfo.conservativeRasterTier = options.ConservativeRasterizationTier;
		}

		//=========================================== DXR ===========================================
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};

		if (SUCCEEDED(gDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
		{
			gPipelineInfo.raytracingTier = options5.RaytracingTier;
		}

		//=========================================== Mesh Shaders ===========================================
		D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};

		if (SUCCEEDED(gDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7))))
		{
			gPipelineInfo.meshShaderTier = options7.MeshShaderTier;
		}

		//=========================================== Command Queue ===========================================
		if (gCommandQueue)
		{
			D3D12_COMMAND_QUEUE_DESC queueDesc = gCommandQueue->GetDesc();
			gPipelineInfo.commandQueueType = queueDesc.Type;
		}
	}

	void CapturePipelineStateStream(const D3D12_PIPELINE_STATE_STREAM_DESC* desc, ID3D12PipelineState* pipelineState)
	{
		if (!desc || !desc->pPipelineStateSubobjectStream || desc->SizeInBytes == 0 || !pipelineState)
			return;

		PipelineStateInfo info{};
		info.pipelineState = pipelineState;

		const uint8_t* streamStart = (const uint8_t*)desc->pPipelineStateSubobjectStream;
		info.streamBlob.assign(streamStart, streamStart + desc->SizeInBytes);

		ParsePipelineStream(desc, info);

		std::lock_guard<std::mutex> lock(gPipelineMutex);
		RegisterKnownPipelineStateLocked(pipelineState);
		gPipelineStates.push_back(info);
		RebindPipelineStateInfoPointerFields(gPipelineStates.back());
		MarkShaderReplacementApplyDirty();
	}


	bool IsKnownPipelineStateLocked(ID3D12PipelineState* pso)
	{
		return !pso || gKnownPipelineStates.find(pso) != gKnownPipelineStates.end();
	}


	void STDMETHODCALLTYPE Hook_SetGraphicsRootSignature(ID3D12GraphicsCommandList* cmdList, ID3D12RootSignature* rootSignature)
	{
		{
			std::lock_guard<std::mutex> lock(gPipelineMutex);
			gCurrentGraphicsRootSignatureByCommandList[cmdList] = rootSignature;
		}

		Original_SetGraphicsRootSignature(cmdList, rootSignature);
	}

	void STDMETHODCALLTYPE Hook_SetComputeRootSignature(ID3D12GraphicsCommandList* cmdList, ID3D12RootSignature* rootSignature)
	{
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
					MarkShaderReplacementApplyDirty();
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
			std::string pointerText = PointerToString(pipelineState);
			std::replace(pointerText.begin(), pointerText.end(), ':', '_');
			std::string path = ShaderInjectorIO::GetUncapturedPSODirectory() + "\\UncapturedPSO_" + Hash::FormatHash(info.cachedBlobHash) + "_" + pointerText + ShaderInjectorIO::extensionBIN;
			ShaderInjectorIO::WriteBinaryFile(path, info.cachedBlob.data(), info.cachedBlob.size());
		}
		gUncapturedPipelineStates.push_back(info);

		char buffer[384];
		sprintf_s(
			buffer,
			"%s uncaptured PSO=%p cachedHash=%s cachedBytes=%zu",
			reason ? reason : "Bound",
			pipelineState,
			info.cachedBlobHash ? Hash::FormatHash(info.cachedBlobHash).c_str() : "<none>",
			(size_t)info.cachedBlobSize);
		ShaderInjectorGUI::WriteToRuntimeLog(buffer);

		if (info.cachedBlobHash)
			MarkShaderReplacementApplyDirty();
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

		{
			std::lock_guard<std::mutex> lock(gPipelineMutex);

			if (gPipelineStateOverridesDirty)
				RebuildPipelineStateOverrideMap();

			if (pso && !IsKnownPipelineStateLocked(pso) && gUntrackedBoundPipelineStates.insert(pso).second)
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

		{
			std::lock_guard<std::mutex> lock(gPipelineMutex);

			if (gPipelineStateOverridesDirty)
				RebuildPipelineStateOverrideMap();

			if (initialState && !IsKnownPipelineStateLocked(initialState) && gUntrackedBoundPipelineStates.insert(initialState).second)
				RecordUncapturedPipelineStateLocked(cmdList, initialState, "CommandList Reset bound initial");

			auto overrideIt = gPipelineStateOverrides.find(initialState);
			if (overrideIt != gPipelineStateOverrides.end() && overrideIt->second)
				boundState = overrideIt->second;
		}

		return Original_ResetGraphicsCommandList(cmdList, allocator, boundState);
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE ROOT SIGNATURE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	HRESULT STDMETHODCALLTYPE Hook_CreateRootSignature(ID3D12Device* device, UINT nodeMask, const void* blobWithRootSignature, SIZE_T blobLengthInBytes, REFIID riid, void** rootSignature)
	{
		HRESULT hr = Original_CreateRootSignature(device, nodeMask, blobWithRootSignature, blobLengthInBytes, riid, rootSignature);

		if (SUCCEEDED(hr) && blobWithRootSignature && blobLengthInBytes > 0 && rootSignature && *rootSignature)
		{
			ID3D12RootSignature* rootSignatureObject = nullptr;
			IUnknown* unknown = reinterpret_cast<IUnknown*>(*rootSignature);
			if (unknown && SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&rootSignatureObject))))
			{
				RootSignatureInfo info{};
				const uint8_t* bytes = static_cast<const uint8_t*>(blobWithRootSignature);
				info.blob.assign(bytes, bytes + blobLengthInBytes);
				info.hash = Hash::HashMemory(blobWithRootSignature, blobLengthInBytes);

				std::lock_guard<std::mutex> lock(gPipelineMutex);
				gRootSignatureInfoByPointer[rootSignatureObject] = info;
				rootSignatureObject->Release();
			}
		}

		return hr;
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
		{
			ComputePipelineInfo info{};

			info.pipelineState = (ID3D12PipelineState*)*ppPipelineState;

			if (desc->CS.pShaderBytecode)
			{
				info.csHash = Hash::HashMemory(desc->CS.pShaderBytecode, desc->CS.BytecodeLength);
				info.csSize = desc->CS.BytecodeLength;
			}

			{
				std::lock_guard<std::mutex> lock(gPipelineMutex);
				RegisterKnownPipelineStateLocked(info.pipelineState);
				gComputePipelines.push_back(info);
			}
		}

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
		{
			GraphicsPipelineInfo info{};

			info.pipelineState = (ID3D12PipelineState*)*ppPipelineState;

			if (desc->VS.pShaderBytecode && desc->VS.BytecodeLength)
			{
				info.vsHash = Hash::HashMemory(desc->VS.pShaderBytecode, desc->VS.BytecodeLength);
				info.vsSize = desc->VS.BytecodeLength;
				info.vsBytecode.assign((const uint8_t*)desc->VS.pShaderBytecode, (const uint8_t*)desc->VS.pShaderBytecode + desc->VS.BytecodeLength);
			}

			if (desc->PS.pShaderBytecode && desc->PS.BytecodeLength)
			{
				info.psHash = Hash::HashMemory(desc->PS.pShaderBytecode, desc->PS.BytecodeLength);
				info.psSize = desc->PS.BytecodeLength;
				info.psBytecode.assign((const uint8_t*)desc->PS.pShaderBytecode, (const uint8_t*)desc->PS.pShaderBytecode + desc->PS.BytecodeLength);
			}

			if (desc->GS.pShaderBytecode && desc->GS.BytecodeLength)
			{
				info.gsHash = Hash::HashMemory(desc->GS.pShaderBytecode, desc->GS.BytecodeLength);
				info.gsSize = desc->GS.BytecodeLength;
				info.gsBytecode.assign((const uint8_t*)desc->GS.pShaderBytecode, (const uint8_t*)desc->GS.pShaderBytecode + desc->GS.BytecodeLength);
			}

			if (desc->HS.pShaderBytecode && desc->HS.BytecodeLength)
			{
				info.hsHash = Hash::HashMemory(desc->HS.pShaderBytecode, desc->HS.BytecodeLength);
				info.hsSize = desc->HS.BytecodeLength;
				info.hsBytecode.assign((const uint8_t*)desc->HS.pShaderBytecode, (const uint8_t*)desc->HS.pShaderBytecode + desc->HS.BytecodeLength);
			}

			if (desc->DS.pShaderBytecode && desc->DS.BytecodeLength)
			{
				info.dsHash = Hash::HashMemory(desc->DS.pShaderBytecode, desc->DS.BytecodeLength);
				info.dsSize = desc->DS.BytecodeLength;
				info.dsBytecode.assign((const uint8_t*)desc->DS.pShaderBytecode, (const uint8_t*)desc->DS.pShaderBytecode + desc->DS.BytecodeLength);
			}

			info.originalDesc = *desc;
			info.originalDesc.VS = { info.vsBytecode.data(), info.vsBytecode.size() };
			info.originalDesc.PS = { info.psBytecode.data(), info.psBytecode.size() };
			info.originalDesc.GS = { info.gsBytecode.empty() ? nullptr : info.gsBytecode.data(), info.gsBytecode.size() };
			info.originalDesc.HS = { info.hsBytecode.empty() ? nullptr : info.hsBytecode.data(), info.hsBytecode.size() };
			info.originalDesc.DS = { info.dsBytecode.empty() ? nullptr : info.dsBytecode.data(), info.dsBytecode.size() };
			info.originalDesc.pRootSignature = desc->pRootSignature;

			if (info.originalDesc.pRootSignature)
				info.originalDesc.pRootSignature->AddRef();

			// Fix up InputLayout
			if (desc->InputLayout.pInputElementDescs && desc->InputLayout.NumElements > 0)
			{
				info.inputElements.assign(desc->InputLayout.pInputElementDescs, desc->InputLayout.pInputElementDescs + desc->InputLayout.NumElements);
				info.originalDesc.InputLayout.pInputElementDescs = info.inputElements.data();
				info.originalDesc.InputLayout.NumElements = (UINT)info.inputElements.size();
			}
			else
			{
				info.originalDesc.InputLayout.pInputElementDescs = nullptr;
				info.originalDesc.InputLayout.NumElements = 0;
			}

			// Fix up StreamOutput
			if (desc->StreamOutput.pSODeclaration && desc->StreamOutput.NumEntries > 0)
			{
				info.soDeclarations.assign(desc->StreamOutput.pSODeclaration, desc->StreamOutput.pSODeclaration + desc->StreamOutput.NumEntries);
				info.originalDesc.StreamOutput.pSODeclaration = info.soDeclarations.data();
			}
			else
			{
				info.originalDesc.StreamOutput.pSODeclaration = nullptr;
				info.originalDesc.StreamOutput.NumEntries = 0;
			}

			if (desc->StreamOutput.pBufferStrides && desc->StreamOutput.NumStrides > 0)
			{
				info.soStrides.assign(desc->StreamOutput.pBufferStrides, desc->StreamOutput.pBufferStrides + desc->StreamOutput.NumStrides);
				info.originalDesc.StreamOutput.pBufferStrides = info.soStrides.data();
			}
			else
			{
				info.originalDesc.StreamOutput.pBufferStrides = nullptr;
				info.originalDesc.StreamOutput.NumStrides = 0;
			}

			// Also zero out CachedPSO — it's tied to the original pipeline cache
			// and will cause E_INVALIDARG if the cache is stale or from a different device
			info.originalDesc.CachedPSO.pCachedBlob = nullptr;
			info.originalDesc.CachedPSO.CachedBlobSizeInBytes = 0;

			{
				std::lock_guard<std::mutex> lock(gPipelineMutex);
				RegisterKnownPipelineStateLocked(info.pipelineState);
				gGraphicsPipelines.push_back(info);
				MarkShaderReplacementApplyDirty();
			}

			//TEST: we should see POSITION, NORMAL, TEXCOORD and other related things
			//IMPORTANT NOTE: we do, so this test passes!
			//for (auto& e : info.inputElements)
			//{
				//char buf[256];
				//sprintf_s(buf, "Semantic=%s", e.SemanticName ? e.SemanticName : "<null>");
				//MessageBoxA(nullptr, buf, "Input Element", MB_OK);
			//}


			//TEST 2: Rebuild from our copied version immediately [PASSED]
			/*
			D3D12_GRAPHICS_PIPELINE_STATE_DESC testDesc = info.originalDesc;

			// Re-point pointers exactly like ProcessPendingRebuilds does
			testDesc.VS = { info.vsBytecode.empty() ? nullptr : info.vsBytecode.data(), info.vsBytecode.size() };
			testDesc.PS = { info.psBytecode.empty() ? nullptr : info.psBytecode.data(), info.psBytecode.size() };
			testDesc.InputLayout.pInputElementDescs = info.inputElements.empty() ? nullptr : info.inputElements.data();
			testDesc.InputLayout.NumElements = (UINT)info.inputElements.size();
			testDesc.StreamOutput.pSODeclaration = info.soDeclarations.empty() ? nullptr : info.soDeclarations.data();
			testDesc.StreamOutput.NumEntries = (UINT)info.soDeclarations.size();
			testDesc.StreamOutput.pBufferStrides = info.soStrides.empty() ? nullptr : info.soStrides.data();
			testDesc.StreamOutput.NumStrides = (UINT)info.soStrides.size();
			testDesc.CachedPSO = { nullptr, 0 };

			ID3D12PipelineState* testPSO = nullptr;

			//important note: we call the original function here, not through device-> otherwise we create a recursion loop and create a fatal application error
			HRESULT testHr = oCreateGraphicsPipelineState(device, &testDesc, riid, (void**)&testPSO);

			if (FAILED(testHr))
			{
				char buf[512];
				sprintf_s(buf, "TEST REBUILD FAILED\nhr=0x%08X", (unsigned)testHr);
				MessageBoxA(nullptr, buf, "PSO Test", MB_OK);
			}
			else
			{
				MessageBoxA(nullptr, "TEST REBUILD SUCCEEDED", "PSO Test", MB_OK);
				testPSO->Release();
			}
			*/
			//IMPORTANT NOTE: the rebuild test succeds!
		}

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

		//MessageBoxA(nullptr, "hit", "PipelineState", MB_OK);

		//NOTE: haven't hit any of these yet, seems to pass
		if (FAILED(hr))
		{
			ShaderInjectorGUI::WriteToRuntimeLog("[HookD3D12]: Hook_CreatePipelineState() not succeded.");
			return hr;
		}

		//NOTE: haven't hit any of these yet, seems to pass
		if (!desc || !ppPipelineState || !*ppPipelineState)
		{
			ShaderInjectorGUI::WriteToRuntimeLog("[HookD3D12]: Hook_CreatePipelineState() not succeded, no objects");
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
			ShaderInjectorGUI::WriteToRuntimeLog("RebuildStreamPSOWithoutStage: marker and null pixel shader blobs are empty, aborting PS marker rebuild");
			return;
		}

		if (!hiddenSelection && targetType == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS && Globals::markerComputeShaderBlob.empty())
		{
			ShaderInjectorGUI::WriteToRuntimeLog("RebuildStreamPSOWithoutStage: marker compute shader blob is empty, aborting CS marker rebuild");
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
			ShaderInjectorGUI::WriteToRuntimeLog("[HookD3D12]: RebuildStreamPSOWithoutStage() Failed QueryInterface for Device2");
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
				ShaderInjectorGUI::WriteToRuntimeLog("[HookD3D12]: RebuildStreamPSOWithoutStage() Stream walk: ptr overran end reading type");
				break;
			}

			auto type = *reinterpret_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(ptr);
			UINT typeIdx = (UINT)type;

			if (typeIdx >= ARRAYSIZE(kSubobjectSizes))
			{
				char unk[256];
				sprintf_s(unk, "[HookD3D12]: RebuildStreamPSOWithoutStage() Unknown typeIdx=%u at offset=%zu, stopping", typeIdx, (size_t)(ptr - patchedBlob.data()));
				ShaderInjectorGUI::WriteToRuntimeLog(unk);
				break;
			}

			size_t subobjectSize = kSubobjectSizes[typeIdx];

			if (ptr + subobjectSize > end)
			{
				ShaderInjectorGUI::WriteToRuntimeLog("[HookD3D12]: RebuildStreamPSOWithoutStage() Stream walk: subobject overruns buffer");
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
			ShaderInjectorGUI::WriteToRuntimeLog("[HookD3D12]: RebuildStreamPSOWithoutStage() Target shader type not found in stream blob, aborting");
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

			if (typeIdx >= ARRAYSIZE(kSubobjectSizes)) break;

			size_t subobjectSize = kSubobjectSizes[typeIdx];
			if (ptr + subobjectSize > end) break;

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
			ShaderInjectorGUI::WriteToRuntimeLog("[HookD3D12]: RebuildStreamPSOWithoutStage() RebuildStreamPSOWithoutStage: failed hr=" + std::to_string((unsigned)hr));
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

		if (replacementIndex < 0 || replacementIndex >= (int)gLoadedShaderReplacements.size())
			return false;

		ShaderReplacement::ShaderReplacementDisk& replacement = gLoadedShaderReplacements[replacementIndex];

		if (replacementIndex >= (int)gLoadedShaderReplacementBlobs.size())
			gLoadedShaderReplacementBlobs.resize(gLoadedShaderReplacements.size());

		std::vector<uint8_t>& blob = gLoadedShaderReplacementBlobs[replacementIndex];

		if (blob.empty() && !replacement.modifiedShaderBlobPath.empty())
		{
			if (ShaderInjectorIO::FileExists(replacement.modifiedShaderBlobPath))
			{
				if (ShaderInjectorIO::LoadDXILBlobFromDisk(replacement.modifiedShaderBlobPath, blob) && !blob.empty())
				{
					ShaderInjectorGUI::WriteToRuntimeLog("GetReplacementBlobForUse: loaded modified blob for " + replacement.name + " bytes=" + std::to_string(blob.size()));
				}
				else
				{
					ShaderInjectorGUI::WriteToRuntimeLog("GetReplacementBlobForUse: failed to load modified blob for " + replacement.name + " path=" + replacement.modifiedShaderBlobPath);
				}
			}
			else
			{
				ShaderInjectorGUI::WriteToRuntimeLog("GetReplacementBlobForUse: modified blob file missing for " + replacement.name + " path=" + replacement.modifiedShaderBlobPath);
			}
		}

		if (!blob.empty())
		{
			outBytecode = blob.data();
			outBytecodeSize = blob.size();
			return true;
		}

		if (replacement.shaderType == ShaderReplacement::PixelShader && !Globals::nullPixelShaderBlob.empty())
		{
			outBytecode = Globals::nullPixelShaderBlob.data();
			outBytecodeSize = Globals::nullPixelShaderBlob.size();
			outUsedFallback = true;
			ShaderInjectorGUI::WriteToRuntimeLog("GetReplacementBlobForUse: using null pixel shader fallback for " + replacement.name);
			return true;
		}

		return false;
	}

	bool RebuildGraphicsPSOWithReplacement(GraphicsPipelineInfo& pipeline, int replacementIndex, uint64_t shaderHash, ShaderReplacement::ShaderType shaderType)
	{
		if (!gDevice || replacementIndex < 0 || replacementIndex >= (int)gLoadedShaderReplacements.size())
			return false;

		ShaderReplacement::ShaderReplacementDisk& replacement = gLoadedShaderReplacements[replacementIndex];
		BackfillReplacementCachedBlobInfo(replacement, pipeline.pipelineState);

		const bool compiledBlobAvailable = replacementIndex < (int)gLoadedShaderReplacementBlobs.size() && !gLoadedShaderReplacementBlobs[replacementIndex].empty();
		const bool compiledBlobOnDisk = !replacement.modifiedShaderBlobPath.empty() && ShaderInjectorIO::FileExists(replacement.modifiedShaderBlobPath);
		if (pipeline.psoWithReplacement && pipeline.activeShaderReplacementName == replacement.name && pipeline.activeShaderReplacementHash == shaderHash && pipeline.activeShaderReplacementType == shaderType)
		{
			if (!pipeline.activeShaderReplacementUsesFallback || (!compiledBlobAvailable && !compiledBlobOnDisk))
				return true;
		}

		if (pipeline.psoWithReplacement)
		{
			UnregisterKnownPipelineStateLocked(pipeline.psoWithReplacement);
			pipeline.psoWithReplacement->Release();
			pipeline.psoWithReplacement = nullptr;
		}

		const void* replacementBytecode = nullptr;
		size_t replacementBytecodeSize = 0;
		bool usedFallback = false;

		if (!GetReplacementBlobForUse(replacementIndex, replacementBytecode, replacementBytecodeSize, usedFallback))
		{
			ShaderInjectorGUI::WriteToRuntimeLog("RebuildGraphicsPSOWithReplacement: replacement blob unavailable for " + replacement.name);
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
			case ShaderReplacement::VertexShader: desc.VS = { replacementBytecode, replacementBytecodeSize }; break;
			case ShaderReplacement::PixelShader: desc.PS = { replacementBytecode, replacementBytecodeSize }; break;
			case ShaderReplacement::GeometryShader: desc.GS = { replacementBytecode, replacementBytecodeSize }; break;
			case ShaderReplacement::HullShader: desc.HS = { replacementBytecode, replacementBytecodeSize }; break;
			case ShaderReplacement::DomainShader: desc.DS = { replacementBytecode, replacementBytecodeSize }; break;
			default: return false;
		}

		HRESULT hr = Original_CreateGraphicsPipelineState(gDevice, &desc, IID_PPV_ARGS(&pipeline.psoWithReplacement));


		if (FAILED(hr))
		{
			ShaderInjectorGUI::WriteToRuntimeLog("RebuildGraphicsPSOWithReplacement: failed hr=" + std::to_string((unsigned)hr) + " replacement=" + replacement.name);
			pipeline.psoWithReplacement = nullptr;
			return false;
		}

		pipeline.activeShaderReplacementName = replacement.name;
		pipeline.activeShaderReplacementType = shaderType;
		pipeline.activeShaderReplacementHash = shaderHash;
		pipeline.activeShaderReplacementUsesFallback = usedFallback;
		RegisterKnownPipelineStateLocked(pipeline.psoWithReplacement);
		gPipelineStateOverridesDirty = true;
		ShaderInjectorGUI::WriteToRuntimeLog("Applied graphics shader replacement: " + replacement.name + (usedFallback ? " (null shader fallback)" : ""));
		return true;
	}

	bool RebuildStreamPSOWithReplacement(PipelineStateInfo& pipeline, int replacementIndex, uint64_t shaderHash, ShaderReplacement::ShaderType shaderType, ID3D12RootSignature* rootSignatureOverride = nullptr)
	{
		if (!gDevice || pipeline.streamBlob.empty() || replacementIndex < 0 || replacementIndex >= (int)gLoadedShaderReplacements.size())
			return false;

		ShaderReplacement::ShaderReplacementDisk& replacement = gLoadedShaderReplacements[replacementIndex];
		BackfillReplacementCachedBlobInfo(replacement, pipeline.pipelineState);

		const bool compiledBlobAvailable = replacementIndex < (int)gLoadedShaderReplacementBlobs.size() && !gLoadedShaderReplacementBlobs[replacementIndex].empty();
		const bool compiledBlobOnDisk = !replacement.modifiedShaderBlobPath.empty() && ShaderInjectorIO::FileExists(replacement.modifiedShaderBlobPath);
		if (pipeline.psoWithReplacement && pipeline.activeShaderReplacementName == replacement.name && pipeline.activeShaderReplacementHash == shaderHash && pipeline.activeShaderReplacementType == shaderType)
		{
			if (!pipeline.activeShaderReplacementUsesFallback || (!compiledBlobAvailable && !compiledBlobOnDisk))
				return true;
		}

		if (pipeline.psoWithReplacement)
		{
			UnregisterKnownPipelineStateLocked(pipeline.psoWithReplacement);
			pipeline.psoWithReplacement->Release();
			pipeline.psoWithReplacement = nullptr;
		}

		const void* replacementBytecode = nullptr;
		size_t replacementBytecodeSize = 0;
		bool usedFallback = false;

		if (!GetReplacementBlobForUse(replacementIndex, replacementBytecode, replacementBytecodeSize, usedFallback))
		{
			ShaderInjectorGUI::WriteToRuntimeLog("RebuildStreamPSOWithReplacement: replacement blob unavailable for " + replacement.name);
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
			ShaderInjectorGUI::WriteToRuntimeLog("RebuildStreamPSOWithReplacement: persisted stream template needs an observed root signature for " + replacement.name);
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

		HRESULT hr = CreatePipelineStateInternal(device2, &patchedDesc, IID_PPV_ARGS(&pipeline.psoWithReplacement));


		device2->Release();

		if (FAILED(hr))
		{
			ShaderInjectorGUI::WriteToRuntimeLog("RebuildStreamPSOWithReplacement: failed hr=" + std::to_string((unsigned)hr) + " replacement=" + replacement.name + " streamBytes=" + std::to_string(patchedBlob.size()) + " root=" + PointerToString(rootSignatureOverride) + " vsBytes=" + std::to_string(pipeline.vsBytecode.size()) + " psBytes=" + std::to_string(pipeline.psBytecode.size()) + " inputElements=" + std::to_string(pipeline.inputElements.size()));
			pipeline.psoWithReplacement = nullptr;
			return false;
		}

		pipeline.activeShaderReplacementName = replacement.name;
		pipeline.activeShaderReplacementType = shaderType;
		pipeline.activeShaderReplacementHash = shaderHash;
		pipeline.activeShaderReplacementUsesFallback = usedFallback;
		RegisterKnownPipelineStateLocked(pipeline.psoWithReplacement);
		gPipelineStateOverridesDirty = true;
		ShaderInjectorGUI::WriteToRuntimeLog("Applied stream shader replacement: " + replacement.name + (usedFallback ? " (null shader fallback)" : ""));
		return true;
	}

	void TryApplyGraphicsReplacement(GraphicsPipelineInfo& pipeline)
	{
		struct Candidate { uint64_t hash; ShaderReplacement::ShaderType type; };
		const Candidate candidates[] =
		{
			{ pipeline.vsHash, ShaderReplacement::VertexShader },
			{ pipeline.psHash, ShaderReplacement::PixelShader },
			{ pipeline.gsHash, ShaderReplacement::GeometryShader },
			{ pipeline.hsHash, ShaderReplacement::HullShader },
			{ pipeline.dsHash, ShaderReplacement::DomainShader },
		};

		for (const Candidate& candidate : candidates)
		{
			const int replacementIndex = FindEnabledShaderReplacement(candidate.hash, candidate.type);

			if (replacementIndex >= 0)
			{
				RebuildGraphicsPSOWithReplacement(pipeline, replacementIndex, candidate.hash, candidate.type);
				return;
			}
		}
	}

	void TryApplyStreamReplacement(PipelineStateInfo& pipeline)
	{
		struct Candidate { uint64_t hash; ShaderReplacement::ShaderType type; };
		const Candidate candidates[] =
		{
			{ pipeline.vsHash, ShaderReplacement::VertexShader },
			{ pipeline.psHash, ShaderReplacement::PixelShader },
			{ pipeline.csHash, ShaderReplacement::ComputeShader },
			{ pipeline.gsHash, ShaderReplacement::GeometryShader },
			{ pipeline.hsHash, ShaderReplacement::HullShader },
			{ pipeline.dsHash, ShaderReplacement::DomainShader },
		};

		for (const Candidate& candidate : candidates)
		{
			const int replacementIndex = FindEnabledShaderReplacement(candidate.hash, candidate.type);

			if (replacementIndex >= 0)
			{
				RebuildStreamPSOWithReplacement(pipeline, replacementIndex, candidate.hash, candidate.type);
				return;
			}
		}
	}

	
	bool TryApplyPersistedStreamTemplateToUncaptured(UncapturedPipelineStateInfo& uncaptured, int replacementIndex, uint64_t shaderHash, ShaderReplacement::ShaderType shaderType, const char* matchMethod)
	{
		if (replacementIndex < 0 || replacementIndex >= (int)gLoadedShaderReplacements.size())
			return false;

		ShaderReplacement::ShaderReplacementDisk& replacement = gLoadedShaderReplacements[replacementIndex];
		ShaderReplacement::ShaderReplacementDisk templateReplacement{};
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
		const bool preferComputeRootSignature = shaderType == ShaderReplacement::ComputeShader || (persistedPipeline.isCompute && !persistedPipeline.isGraphics);
		if (preferComputeRootSignature)
			observedRootSignature = uncaptured.observedComputeRootSignature ? uncaptured.observedComputeRootSignature : uncaptured.observedGraphicsRootSignature;
		else
			observedRootSignature = uncaptured.observedGraphicsRootSignature ? uncaptured.observedGraphicsRootSignature : uncaptured.observedComputeRootSignature;

		ID3D12RootSignature* persistedRootSignature = GetOrCreatePersistedRootSignature(templateReplacement);
		bool attemptedAnyRootSignature = false;

		auto TryRebuildWithRootSignature = [&](ID3D12RootSignature* rootSignatureForRebuild, const char* rootSignatureSource) -> bool
		{
			if (!rootSignatureForRebuild)
				return false;

			attemptedAnyRootSignature = true;
			ShaderInjectorGUI::WriteToRuntimeLog(std::string("Uncaptured persisted stream rebuild using ") + rootSignatureSource + ": " + replacement.name + templateLogSuffix);

			if (!RebuildStreamPSOWithReplacement(persistedPipeline, replacementIndex, shaderHash, shaderType, rootSignatureForRebuild))
				return false;

			uncaptured.replacementPipelineState = persistedPipeline.psoWithReplacement;
			uncaptured.activeShaderReplacementName = replacement.name;
			uncaptured.activeShaderReplacementType = shaderType;
			uncaptured.activeShaderReplacementHash = shaderHash;
			gPipelineStateOverrides[uncaptured.pipelineState] = persistedPipeline.psoWithReplacement;
			ShaderInjectorGUI::WriteToRuntimeLog(std::string("Applied uncaptured PSO replacement from persisted stream template by ") + matchMethod + " using " + rootSignatureSource + ": " + replacement.name + templateLogSuffix);
			return true;
		};

		if (TryRebuildWithRootSignature(observedRootSignature, "observed command-list root signature"))
			return true;

		if (persistedRootSignature && persistedRootSignature != observedRootSignature)
		{
			if (observedRootSignature)
				ShaderInjectorGUI::WriteToRuntimeLog("Observed root signature rebuild failed; retrying persisted root signature blob: " + replacement.name + templateLogSuffix);

			if (TryRebuildWithRootSignature(persistedRootSignature, "persisted root signature blob"))
				return true;
		}

		if (!attemptedAnyRootSignature)
			ShaderInjectorGUI::WriteToRuntimeLog("Uncaptured PSO matched persisted stream template, but no root signature is available: " + replacement.name + templateLogSuffix);

		return false;
	}

	bool TryApplyUncapturedReplacement(UncapturedPipelineStateInfo& uncaptured)
	{
		if (!uncaptured.pipelineState || !uncaptured.cachedBlobHash)
			return false;

		int replacementIndex = FindEnabledShaderReplacementByCachedBlob(uncaptured.cachedBlobHash);
		const char* matchMethod = "cached blob hash";

		if (replacementIndex < 0)
		{
			replacementIndex = FindUniqueEnabledShaderReplacementByCachedBlobSize(uncaptured.cachedBlobSize);
			matchMethod = "unique cached blob size fallback";

			if (replacementIndex >= 0)
			{
				const ShaderReplacement::ShaderReplacementDisk& candidate = gLoadedShaderReplacements[replacementIndex];
				ShaderInjectorGUI::WriteToRuntimeLog(
					"Uncaptured PSO size fallback candidate: replacement=" + candidate.name +
					" uncapturedHash=" + Hash::FormatHash(uncaptured.cachedBlobHash) +
					" replacementCacheHash=" + candidate.pipelineCachedBlobHash +
					" cacheBytes=" + std::to_string((size_t)uncaptured.cachedBlobSize));
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
				const int cachedSizeCandidateCount = CountEnabledShaderReplacementsByCachedBlobSize(uncaptured.cachedBlobSize);

				for (const auto& replacement : gLoadedShaderReplacements)
				{
					if (!replacement.enabled)
						continue;

					enabledCount++;
					if (!replacement.pipelineCachedBlobHash.empty())
						cachedHashCount++;
				}

				ShaderInjectorGUI::WriteToRuntimeLog(
					"Uncaptured PSO has no replacement match: cachedHash=" + Hash::FormatHash(uncaptured.cachedBlobHash) +
					" cachedBytes=" + std::to_string((size_t)uncaptured.cachedBlobSize) +
					" enabledReplacements=" + std::to_string(enabledCount) +
					" replacementsWithCachedHash=" + std::to_string(cachedHashCount) +
					" replacementsWithSameCacheBytes=" + std::to_string(cachedSizeCandidateCount));
				noMatchLogCount++;
			}

			return false;
		}

		ShaderReplacement::ShaderReplacementDisk& replacement = gLoadedShaderReplacements[replacementIndex];
		const uint64_t shaderHash = Hash::ParseHashText(replacement.originalShaderBytecodeHash);

		if (!shaderHash || replacement.shaderType == ShaderReplacement::Unknown)
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
					uncaptured.activeShaderReplacementName = replacement.name;
					uncaptured.activeShaderReplacementType = replacement.shaderType;
					uncaptured.activeShaderReplacementHash = shaderHash;
					gPipelineStateOverrides[uncaptured.pipelineState] = pipeline.psoWithReplacement;
					ShaderInjectorGUI::WriteToRuntimeLog(std::string("Applied uncaptured PSO replacement by ") + matchMethod + ": " + replacement.name);
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
					uncaptured.activeShaderReplacementName = replacement.name;
					uncaptured.activeShaderReplacementType = replacement.shaderType;
					uncaptured.activeShaderReplacementHash = shaderHash;
					gPipelineStateOverrides[uncaptured.pipelineState] = pipeline.psoWithReplacement;
					ShaderInjectorGUI::WriteToRuntimeLog(std::string("Applied uncaptured PSO replacement by ") + matchMethod + ": " + replacement.name);
					return true;
				}
			}
		}

		for (auto& pipeline : gGraphicsPipelines)
		{
			if (GraphicsPipelineMatchesReplacementTemplate(pipeline, replacement) && RebuildGraphicsPSOWithReplacement(pipeline, replacementIndex, shaderHash, replacement.shaderType))
			{
				uncaptured.replacementPipelineState = pipeline.psoWithReplacement;
				uncaptured.activeShaderReplacementName = replacement.name;
				uncaptured.activeShaderReplacementType = replacement.shaderType;
				uncaptured.activeShaderReplacementHash = shaderHash;
				gPipelineStateOverrides[uncaptured.pipelineState] = pipeline.psoWithReplacement;
				ShaderInjectorGUI::WriteToRuntimeLog("Applied uncaptured PSO replacement by matching graphics template: " + replacement.name);
				return true;
			}
		}

		for (auto& pipeline : gPipelineStates)
		{
			if (StreamPipelineMatchesReplacementTemplate(pipeline, replacement) && RebuildStreamPSOWithReplacement(pipeline, replacementIndex, shaderHash, replacement.shaderType))
			{
				uncaptured.replacementPipelineState = pipeline.psoWithReplacement;
				uncaptured.activeShaderReplacementName = replacement.name;
				uncaptured.activeShaderReplacementType = replacement.shaderType;
				uncaptured.activeShaderReplacementHash = shaderHash;
				gPipelineStateOverrides[uncaptured.pipelineState] = pipeline.psoWithReplacement;
				ShaderInjectorGUI::WriteToRuntimeLog("Applied uncaptured PSO replacement by matching stream template: " + replacement.name);
				return true;
			}
		}

		if ((replacement.sourceList == "Stream" || !replacement.pipelineStreamBlobPath.empty()) &&
			TryApplyPersistedStreamTemplateToUncaptured(uncaptured, replacementIndex, shaderHash, replacement.shaderType, matchMethod))
		{
			return true;
		}

		uncaptured.attemptedReplacement = true;
		ShaderInjectorGUI::WriteToRuntimeLog("Uncaptured PSO matched replacement cached blob, but no rebuild template is currently available: " + replacement.name);
		return false;
	}

	void ApplyShaderReplacementPSOs()
	{
		if (!Globals::gShaderInjectorEnabled)
			return;

		if (!gShaderReplacementApplyDirty)
			return;

		if (!gLoadedShaderReplacementsOnce)
			RefreshLoadedShaderReplacements();

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
		gShaderReplacementApplyDirty = false;
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
					//desc.PS = { p.psBytecode.empty() ? nullptr : p.psBytecode.data(), p.psBytecode.size() }; //keep this comment, but this line feeds back in original parsed bytecode. important test to verify if we can rebuild using the original bytecode
					const bool hiddenSelection = gShaderSelectionStyle == ShaderSelectionStyle::Hidden;
					const std::vector<uint8_t>& markerBlob = !Globals::markerPixelShaderBlob.empty() ? Globals::markerPixelShaderBlob : Globals::nullPixelShaderBlob;
					desc.PS = hiddenSelection || markerBlob.empty() ? D3D12_SHADER_BYTECODE{ nullptr, 0 } : D3D12_SHADER_BYTECODE{ markerBlob.data(), markerBlob.size() };
					//desc.PS = { kNullPS, sizeof(kNullPS) }; //get an issue here with our custom shader
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

	void InstallPipelineHooksForDevice(ID3D12Device* device)
	{
		if (!device)
			return;

		void** deviceVTable = *reinterpret_cast<void***>(device);
		void* deviceVTableKey = deviceVTable;

		if (!gPipelineHookedDeviceVTables.insert(deviceVTableKey).second)
			return;

		MH_STATUS statusGraphicsPipelineCreate = MH_CreateHook(deviceVTable[VTableIndex::indexCreateGraphicsPipelineState], &Hook_CreateGraphicsPipelineState, reinterpret_cast<void**>(&Original_CreateGraphicsPipelineState));
		MH_STATUS statusGraphicsPipelineEnable = MH_EnableHook(deviceVTable[VTableIndex::indexCreateGraphicsPipelineState]);
		
		MH_STATUS statusComputePipelineCreate = MH_CreateHook(deviceVTable[VTableIndex::indexCreateComputePipelineState], &Hook_CreateComputePipelineState, reinterpret_cast<void**>(&Original_CreateComputePipelineState));
		MH_STATUS statusComputePipelineEnable = MH_EnableHook(deviceVTable[VTableIndex::indexCreateComputePipelineState]);

		MH_STATUS statusRootSignatureCreate = MH_CreateHook(deviceVTable[VTableIndex::indexCreateRootSignature], &Hook_CreateRootSignature, reinterpret_cast<void**>(&Original_CreateRootSignature));
		MH_STATUS statusRootSignatureEnable = MH_EnableHook(deviceVTable[VTableIndex::indexCreateRootSignature]);

		ID3D12Device2* device2 = nullptr;

		if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device2))))
		{
			void** device2VTable = *reinterpret_cast<void***>(device2);

			MH_STATUS statusCreatePipelineStateCreate = MH_CreateHook(device2VTable[VTableIndex::indexCreatePipelineState], &Hook_CreatePipelineState, reinterpret_cast<void**>(&Original_CreatePipelineState));
			MH_STATUS statusCreatePipelineStateEnable = MH_EnableHook(device2VTable[VTableIndex::indexCreatePipelineState]);

			if (statusCreatePipelineStateCreate == MH_OK && statusCreatePipelineStateEnable == MH_OK)
				ShaderInjectorGUI::WriteToRuntimeLog("CreatePipelineState hook installed");

			device2->Release();
		}

		ID3D12Device1* device1 = nullptr;

		if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&device1))))
		{
			void** device1VTable = *reinterpret_cast<void***>(device1);

			MH_STATUS statusCreatePipelineLibraryCreate = MH_CreateHook(device1VTable[VTableIndex::indexCreatePipelineLibrary], &Hook_CreatePipelineLibrary, reinterpret_cast<void**>(&Original_CreatePipelineLibrary));
			MH_STATUS statusCreatePipelineLibraryEnable = MH_EnableHook(device1VTable[VTableIndex::indexCreatePipelineLibrary]);

			device1->Release();

			if (statusCreatePipelineLibraryCreate == MH_OK && statusCreatePipelineLibraryEnable == MH_OK)
				ShaderInjectorGUI::WriteToRuntimeLog("CreatePipelineLibrary hook installed");
		}

		if (statusGraphicsPipelineCreate == MH_OK && statusGraphicsPipelineEnable == MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLog("CreateGraphicsPipelineState hook installed");

		if (statusComputePipelineCreate == MH_OK && statusComputePipelineEnable == MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLog("CreateComputePipelineState hook installed");

		if (statusRootSignatureCreate == MH_OK && statusRootSignatureEnable == MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLog("CreateRootSignature hook installed");
	}

	void InstallPipelineHooks()
	{
		InstallPipelineHooksForDevice(gDevice);
	}

	HRESULT WINAPI Hook_D3D12CreateDevice(IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid, void** ppDevice)
	{
		HRESULT hr = Original_D3D12CreateDevice(pAdapter, MinimumFeatureLevel, riid, ppDevice);

		if (SUCCEEDED(hr) && ppDevice && *ppDevice)
		{
			ID3D12Device* device = nullptr;
			IUnknown* unknown = reinterpret_cast<IUnknown*>(*ppDevice);

			if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&device))))
			{
				InstallPipelineHooksForDevice(device);
				ShaderInjectorGUI::WriteToRuntimeLog("D3D12CreateDevice captured device and installed pipeline hooks");
				device->Release();
			}
		}

		return hr;
	}

	bool InstallD3D12CreateDeviceHook(HMODULE d3d12Module)
	{
		if (gD3D12CreateDeviceHookInstalled)
			return true;

		if (!d3d12Module)
			return false;

		void* createDeviceAddress = reinterpret_cast<void*>(GetProcAddress(d3d12Module, "D3D12CreateDevice"));

		if (!createDeviceAddress)
		{
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12 | InstallD3D12CreateDeviceHook | D3D12CreateDevice export not found");
			return false;
		}

		MH_STATUS createStatus = MH_CreateHook(createDeviceAddress, reinterpret_cast<void*>(&Hook_D3D12CreateDevice), reinterpret_cast<void**>(&Original_D3D12CreateDevice));

		if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED)
		{
			ShaderInjectorGUI::WriteToRuntimeLog(std::string("HookD3D12 | InstallD3D12CreateDeviceHook | D3D12CreateDevice hook create failed: ") + MH_StatusToString(createStatus));
			return false;
		}

		MH_STATUS enableStatus = MH_EnableHook(createDeviceAddress);

		if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED)
		{
			ShaderInjectorGUI::WriteToRuntimeLog(std::string("HookD3D12 | InstallD3D12CreateDeviceHook | D3D12CreateDevice hook enable failed: ") + MH_StatusToString(enableStatus));
			return false;
		}

		gD3D12CreateDeviceHookInstalled = true;
		ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12 | InstallD3D12CreateDeviceHook | D3D12CreateDevice hook installed");
		return true;
	}

	void InstallCommandListHooksForCommandList(ID3D12GraphicsCommandList* commandList)
	{
		if (!commandList)
			return;

		void** commandListVTable = *reinterpret_cast<void***>(commandList);
		void* commandListVTableKey = commandListVTable;

		if (!gCommandListHookedVTables.insert(commandListVTableKey).second)
			return;

		MH_STATUS resetCreate = MH_CreateHook(commandListVTable[VTableIndex::indexResetGraphicsCommandList], &Hook_ResetGraphicsCommandList, reinterpret_cast<void**>(&Original_ResetGraphicsCommandList));
		MH_STATUS resetEnable = MH_EnableHook(commandListVTable[VTableIndex::indexResetGraphicsCommandList]);

		MH_STATUS setPipelineCreate = MH_CreateHook(commandListVTable[VTableIndex::indexSetPipelineState], &Hook_SetPipelineState, reinterpret_cast<void**>(&Original_SetPipelineState));
		MH_STATUS setPipelineEnable = MH_EnableHook(commandListVTable[VTableIndex::indexSetPipelineState]);

		MH_STATUS setComputeRootCreate = MH_CreateHook(commandListVTable[VTableIndex::indexSetComputeRootSignature], &Hook_SetComputeRootSignature, reinterpret_cast<void**>(&Original_SetComputeRootSignature));
		MH_STATUS setComputeRootEnable = MH_EnableHook(commandListVTable[VTableIndex::indexSetComputeRootSignature]);

		MH_STATUS setGraphicsRootCreate = MH_CreateHook(commandListVTable[VTableIndex::indexSetGraphicsRootSignature], &Hook_SetGraphicsRootSignature, reinterpret_cast<void**>(&Original_SetGraphicsRootSignature));
		MH_STATUS setGraphicsRootEnable = MH_EnableHook(commandListVTable[VTableIndex::indexSetGraphicsRootSignature]);

		if (resetCreate == MH_OK && resetEnable == MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12 | InstallCommandListHooksForCommandList | CommandList Reset hook installed");

		if (setPipelineCreate == MH_OK && setPipelineEnable == MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12 | InstallCommandListHooksForCommandList | SetPipelineState hook installed");

		if (setComputeRootCreate == MH_OK && setComputeRootEnable == MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12 | InstallCommandListHooksForCommandList | SetComputeRootSignature hook installed");

		if (setGraphicsRootCreate == MH_OK && setGraphicsRootEnable == MH_OK)
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12 | HandlePresentD3D12 | SetGraphicsRootSignature hook installed");

		gCommandListHookInstalled = true;
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
				ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12 | HandlePresentD3D12 | Present1 hook active");
				gLoggedPresent1Hook = true;
			}
		}
		else if (!gLoggedPresentHook)
		{
			ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12 | HandlePresentD3D12 | Present hook active");
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
			//ShaderInjectorGUI::WriteToRuntimeLog("Hook_Present1D3D12: gInitialized = TRUE");
		//else
			//ShaderInjectorGUI::WriteToRuntimeLog("Hook_Present1D3D12: gInitialized = FALSE");
	
		DXGI_SWAP_CHAIN_DESC startupSwapChainDesc = {};

		if (!gInitialized && !IsSwapChainReadyForOverlayInitialization(pSwapChain, startupSwapChainDesc))
			return CallOriginalPresent();

		//IMPORTANT NOTE: we do hit this point after x amount of startup frames where we continue with initalization
		//MessageBoxA(nullptr, "Hook_Present1D3D12: startup frames beyond 300, continuing", "Shader Injector", MB_OK);

		ProcessPendingRebuilds(); // <-- add here, before gInitialized check and before ImGui
		ApplyShaderReplacementPSOs();

		///*
		//this will execute first because when application starts, this is not set to true
		if (!gInitialized)
		{
			//ShaderInjectorGUI::WriteToRuntimeLog("[HookD3D12] Initializing ImGui on first Present1.");

			//IMPORTANT NOTE: this seems to pass fortunately, it doesn't fail
			if (!gDevice && FAILED(pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)&gDevice))) 
			{
				ShaderInjectorGUI::WriteToRuntimeLog("Hook_Present1D3D12: GetDevice fail");
				return CallOriginalPresent();
			}

			if (!gDevice2)
			{
				HRESULT hr = gDevice->QueryInterface(IID_PPV_ARGS(&gDevice2));

				if (FAILED(hr))
				{
					ShaderInjectorGUI::WriteToRuntimeLog("Failed to get ID3D12Device2");
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
				MessageBoxA(nullptr, "hookPresent1D3D12: CreateDescriptorHeap RTV fail", "Shader Injector", MB_OK);
				return CallOriginalPresent();
			}

			heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

			//IMPORTANT NOTE: this seems to pass fortunately, it doesn't fail
			if (FAILED(gDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&gHeapSRV)))) 
			{
				MessageBoxA(nullptr, "hookPresent1D3D12: CreateDescriptorHeap SRV fail", "Shader Injector", MB_OK);
				return CallOriginalPresent();
			}

			// Allocate frame contexts
			gFrameContexts = new FrameContext[gBufferCount];
			ZeroMemory(gFrameContexts, sizeof(FrameContext) * gBufferCount);

			// Create command allocator for each frame
			//IMPORTANT NOTE: this seems to pass fortunately, it doesn't fail
			for (UINT i = 0; i < gBufferCount; ++i)
			{
				if (FAILED(gDevice->CreateCommandAllocator(
					D3D12_COMMAND_LIST_TYPE_DIRECT,
					IID_PPV_ARGS(&gFrameContexts[i].allocator)))) 
				{
					MessageBoxA(nullptr, "hookPresent1D3D12: CreateCommandAllocator fail", "Shader Injector", MB_OK);
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
			//MessageBoxA(nullptr, "[HookD3D12] ImGui initialized.", "Shader Injector", MB_OK);

			HookInput::Initalize(desc.OutputWindow);

			if (!gOverlayFence) 
			{
				//IMPORTANT NOTE: this seems to pass fortunately, it doesn't fail
				if (FAILED(gDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gOverlayFence)))) 
				{
					MessageBoxA(nullptr, "CreateFence fail", "Shader Injector", MB_OK);
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
					sprintf_s(buffer, "[HookD3D12] Failed to create fence event: %lu", GetLastError());
					MessageBoxA(nullptr, buffer, "MinHook", MB_OK);
				}
			}

			GatherPipelineInfo(pSwapChain);
			InstallPipelineHooks();
		
			// Hook CommandQueue and Fence are already captured by minhook
			gInitialized = true;
			if (!gLoggedOverlayInitialized)
			{
				ShaderInjectorIO::WriteToLogFile("[HookD3D12] Overlay initialized from present path");
				gLoggedOverlayInitialized = true;
			}
		}

		InstallCommandListHooks();

		if (GetAsyncKeyState(Globals::keyOpenShaderInjectorGUI) & 1)
		{
			Globals::gShowShaderInjectorGUI = !Globals::gShowShaderInjectorGUI;
			//WriteToRuntimeLog(std::string("Shader Injector GUI ") + (gShowShaderInjectorGUI ? "Enabled" : "Disabled"));
		}

		if (GetAsyncKeyState(Globals::keyToggleShaderInjector) & 1)
		{
			Globals::gShaderInjectorEnabled = !Globals::gShaderInjectorEnabled;
			MarkShaderReplacementApplyDirty();
			//WriteToRuntimeLog(std::string("Shader Injector ") + (gShaderInjectorEnabled ? "Enabled" : "Disabled"));
		}

		if (!Globals::gShowShaderInjectorGUI || gOverlayRenderingDisabled)
			return CallOriginalPresent();

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
			guiContext.fpsCounterActive = &FPSCounter::gFPSCounterActive;
			guiContext.fps = FPSCounter::gCurrentFPS;
			guiContext.frameTimeMs = FPSCounter::gCurrentFrameTimeMS;
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
						//DebugLog("[HookD3D12] WaitForSingleObject timeout");
						canRender = false;
					}
					else if (waitRes != WAIT_OBJECT_0) 
					{
						//DebugLog("[HookD3D12] WaitForSingleObject failed: %lu", GetLastError());
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
				//DebugLog("[HookD3D12] Skipping ImGui render for this frame");
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
				//DebugLog("[HookD3D12] CommandQueue not set, skipping ExecuteCommandLists.");
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
								release();
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
							ShaderInjectorIO::WriteToLogFile("[HookD3D12] Captured direct command queue");
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

		HRESULT hr = gCommandQueue->Signal(
			gOverlayFence,
			gOverlayFenceValue);

		if (FAILED(hr))
		{
			ShaderInjectorGUI::WriteToRuntimeLog("[HookD3D12] Overlay fence signal failed during resize teardown.");
			return false;
		}

		if (gOverlayFence->GetCompletedValue() >= gOverlayFenceValue)
			return true;

		hr = gOverlayFence->SetEventOnCompletion(
			gOverlayFenceValue,
			gFenceEvent);

		if (FAILED(hr))
		{
			ShaderInjectorGUI::WriteToRuntimeLog("[HookD3D12] Overlay fence SetEventOnCompletion failed during resize teardown.");
			return false;
		}

		DWORD waitResult = WaitForSingleObject(gFenceEvent, timeoutMs);
		if (waitResult != WAIT_OBJECT_0)
		{
			ShaderInjectorGUI::WriteToRuntimeLog("[HookD3D12] Overlay fence wait timed out during resize teardown; releasing overlay resources anyway.");
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
		sprintf_s(buffer, "[HookD3D12] ResizeBuffers %ux%u Buffers=%u", Width, Height, BufferCount);
		ShaderInjectorGUI::WriteToRuntimeLog(buffer);

		// Release every overlay object that depends on swap-chain buffers or descriptor heaps before ResizeBuffers.
		// ImGui owns a font texture/SRV through the DX12 backend, so a partial back-buffer release can corrupt the UI after resize.
		gOverlayRenderingDisabled = true;
		if (gInitialized || gFrameContexts || gHeapRTV || gHeapSRV || gCommandList)
		{
			WaitForOverlayGPUIdle();
			ReleaseOverlaySwapChainResources(true);
		}

		HRESULT hr = Original_ResizeBuffersD3D12(
			pSwapChain,
			BufferCount,
			Width,
			Height,
			NewFormat,
			SwapChainFlags);

		if (FAILED(hr))
		{
			sprintf_s(buffer, "[HookD3D12] ResizeBuffers FAILED hr=0x%08X", (UINT)hr);
			ShaderInjectorGUI::WriteToRuntimeLog(buffer);
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
		gLastResizeBuffersTick = GetTickCount64();
		gLoggedResizeCooldown = false;
		ResetOverlayStartupGate();

		ShaderInjectorGUI::WriteToRuntimeLog("[HookD3D12] ResizeBuffers succeeded. Overlay recreation deferred until resize settles.");

		return hr;
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE PIPELINE LIBRARY |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE PIPELINE LIBRARY |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| CREATE PIPELINE LIBRARY |||||||||||||||||||||||||||||||||||||||||||||||||||||

	HRESULT __stdcall Hook_LoadGraphicsPipeline(
		ID3D12PipelineLibrary* library,
		LPCWSTR name,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,
		REFIID riid,
		void** ppPipelineState)
	{
		HRESULT hr = Original_LoadGraphicsPipeline(library, name, desc, riid, ppPipelineState);

		if (SUCCEEDED(hr) && desc && ppPipelineState && *ppPipelineState)
		{
			CaptureGraphicsPipelineState(desc, (ID3D12PipelineState*)*ppPipelineState);
			ShaderInjectorGUI::WriteToRuntimeLog("PipelineLibrary LoadGraphicsPipeline captured cached PSO");
		}

		return hr;
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| LOAD COMPUTE PIPELINE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| LOAD COMPUTE PIPELINE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| LOAD COMPUTE PIPELINE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	HRESULT __stdcall Hook_LoadComputePipeline(
		ID3D12PipelineLibrary* library,
		LPCWSTR name,
		const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc,
		REFIID riid,
		void** ppPipelineState)
	{
		HRESULT hr = Original_LoadComputePipeline(library, name, desc, riid, ppPipelineState);

		if (SUCCEEDED(hr) && desc && ppPipelineState && *ppPipelineState)
		{
			ComputePipelineInfo info{};
			info.pipelineState = (ID3D12PipelineState*)*ppPipelineState;

			if (desc->CS.pShaderBytecode && desc->CS.BytecodeLength)
			{
				info.csHash = Hash::HashMemory(desc->CS.pShaderBytecode, desc->CS.BytecodeLength);
				info.csSize = desc->CS.BytecodeLength;
			}

			std::lock_guard<std::mutex> lock(gPipelineMutex);
			gComputePipelines.push_back(info);
			ShaderInjectorGUI::WriteToRuntimeLog("PipelineLibrary LoadComputePipeline captured cached PSO");
		}

		return hr;
	}

	HRESULT __stdcall Hook_LoadPipeline(
		ID3D12PipelineLibrary1* library,
		LPCWSTR name,
		const D3D12_PIPELINE_STATE_STREAM_DESC* desc,
		REFIID riid,
		void** ppPipelineState)
	{
		HRESULT hr = Original_LoadPipeline(library, name, desc, riid, ppPipelineState);

		if (SUCCEEDED(hr) && desc && ppPipelineState && *ppPipelineState)
		{
			CapturePipelineStateStream(desc, (ID3D12PipelineState*)*ppPipelineState);
			ShaderInjectorGUI::WriteToRuntimeLog("PipelineLibrary1 LoadPipeline captured cached stream PSO");
		}

		return hr;
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| STORE PIPELINE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| STORE PIPELINE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| STORE PIPELINE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	HRESULT __stdcall Hook_StorePipeline(
		ID3D12PipelineLibrary* library,
		LPCWSTR name,
		ID3D12PipelineState* pso)
	{
		if (name)
		{
			//LOG("StorePipeline: %ws", name);
		}

		return Original_StorePipeline(
			library,
			name,
			pso);
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| GET SERIALIZED SIZE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| GET SERIALIZED SIZE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| GET SERIALIZED SIZE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	SIZE_T __stdcall Hook_GetSerializedSize(ID3D12PipelineLibrary* library)
	{
		SIZE_T size = Original_GetSerializedSize(library);

		//LOG("Pipeline Library Size: %llu", size);

		return size;
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SERIALIZE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SERIALIZE |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SERIALIZE |||||||||||||||||||||||||||||||||||||||||||||||||||||

	HRESULT __stdcall Hook_Serialize(
		ID3D12PipelineLibrary* library,
		void* data,
		SIZE_T dataSize)
	{
		HRESULT hr = Original_Serialize(library, data, dataSize);

		if (SUCCEEDED(hr))
		{
			//LOG("Serialize Pipeline Library");
			//std::filesystem::create_directories("PipelineCaches");
			//std::ofstream file("PipelineCaches/SerializedLibrary.bin", std::ios::binary);
			//file.write((const char*)data, dataSize);
		}

		return hr;
	}

	void HookPipelineLibrary(ID3D12PipelineLibrary* library)
	{
		if (!library)
			return;

		if (g_HookedLibraries.find(library) != g_HookedLibraries.end())
			return;

		g_HookedLibraries.insert(library);

		void** vtable = *(void***)(library);

		// ID3D12PipelineLibrary inherits ID3D12DeviceChild, so GetDevice is slot 7.
		MH_CreateHook(vtable[VTableIndex::indexStorePipeline], Hook_StorePipeline, reinterpret_cast<void**>(&Original_StorePipeline));
		MH_CreateHook(vtable[VTableIndex::indexLoadGraphicsPipeline], Hook_LoadGraphicsPipeline, reinterpret_cast<void**>(&Original_LoadGraphicsPipeline));
		MH_CreateHook(vtable[VTableIndex::indexLoadComputePipeline], Hook_LoadComputePipeline, reinterpret_cast<void**>(&Original_LoadComputePipeline));
		MH_CreateHook(vtable[VTableIndex::indexGetSerializedSize], Hook_GetSerializedSize, reinterpret_cast<void**>(&Original_GetSerializedSize));
		MH_CreateHook(vtable[VTableIndex::indexSerialize], Hook_Serialize, reinterpret_cast<void**>(&Original_Serialize));

		ID3D12PipelineLibrary1* library1 = nullptr;

		if (SUCCEEDED(library->QueryInterface(IID_PPV_ARGS(&library1))))
		{
			void** vtable1 = *(void***)(library1);
			MH_CreateHook(vtable1[VTableIndex::indexLoadPipeline], Hook_LoadPipeline, reinterpret_cast<void**>(&Original_LoadPipeline));
			library1->Release();
		}

		MH_EnableHook(MH_ALL_HOOKS);

		ShaderInjectorGUI::WriteToRuntimeLog("Pipeline library hooks installed");
	}	

	HRESULT __stdcall Hook_CreatePipelineLibrary(
		ID3D12Device1* device,
		const void* pLibraryBlob,
		SIZE_T blobLength,
		REFIID riid,
		void** ppPipelineLibrary)
	{
		ShaderInjectorGUI::WriteToRuntimeLog("CreatePipelineLibrary");
		ShaderInjectorGUI::WriteToRuntimeLog("Blob Size: " + std::to_string(blobLength));

		if (pLibraryBlob && blobLength > 0)
		{
			//std::filesystem::create_directories("PipelineCaches");
			//std::ofstream file("PipelineCaches/PipelineLibrary.bin", std::ios::binary);
			//file.write((const char*)pLibraryBlob, blobLength);
		}

		HRESULT hr = Original_CreatePipelineLibrary(
			device,
			pLibraryBlob,
			blobLength,
			riid,
			ppPipelineLibrary);

		if (SUCCEEDED(hr) &&
			ppPipelineLibrary &&
			*ppPipelineLibrary)
		{
			auto* library =
				(ID3D12PipelineLibrary*)(*ppPipelineLibrary);

			HookPipelineLibrary(library);
		}

		return hr;
	}

	void release()
	{
		//ShaderInjectorGUI::WriteToRuntimeLog("[HookD3D12] Releasing resources and hooks.");

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

		for (auto& persistedRootSignature : gPersistedRootSignaturesByPath)
		{
			if (persistedRootSignature.second)
				persistedRootSignature.second->Release();
		}
		gPersistedRootSignaturesByPath.clear();
		gRootSignatureInfoByPointer.clear();
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