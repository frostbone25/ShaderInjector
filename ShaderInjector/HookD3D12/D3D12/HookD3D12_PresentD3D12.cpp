#include "HookD3D12HookHandlers.h"

#include "HookD3D12RuntimeState.h"
#include "../HookD3D12OverlayStartup.h"
#include "../HookD3D12RenderPass.h"
#include "FPSCounter.h"
#include "Globals.h"
#include "GUI/ShaderInjectorGUI.h"
#include "HookInput.h"
#include "IO/ShaderInjectorIO.h"
#include "Performance/PerformanceMetrics.h"
#include "RenderDoc/RenderDocIntegration.h"
#include "RenderPass/RenderPassRuntime.h"
#include "StringHelper.h"

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

namespace HookD3D12
{
	static HRESULT HandlePresentD3D12(
		IDXGISwapChain3* pSwapChain,
		UINT SyncInterval,
		UINT Flags,
		const DXGI_PRESENT_PARAMETERS* pParams,
		bool usePresent1,
		FunctionPresentD3D12 presentOverride = nullptr,
		FunctionPresent1D3D12 present1Override = nullptr)
	{
		FPSCounter::UpdateFPSCounter();
		if (PerformanceMetrics::RecordPresentAndMaybeLog())
			RenderPassRuntime::LogPerformanceSnapshot();

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
			HRESULT presentResult = E_FAIL;

			FunctionPresentD3D12 present = presentOverride ? presentOverride : Original_PresentD3D12;
			FunctionPresent1D3D12 present1 = present1Override ? present1Override : Original_Present1D3D12;

			if (usePresent1 && present1)
				presentResult = present1(pSwapChain, SyncInterval, Flags, pParams);
			else if (present)
				presentResult = present(pSwapChain, SyncInterval, Flags);

			if (FAILED(presentResult))
				LogOverlayDeviceFailure(usePresent1 ? "Present1" : "Present", presentResult);

			return presentResult;
		};

		if ((Flags & DXGI_PRESENT_TEST) != 0)
			return CallOriginalPresent();

		RenderPassRuntime::AdvanceFrame();

		// ExecuteCommandLists records the most recently submitted direct queue. The queue
		// immediately preceding Present is the safest fallback when the swap chain was
		// created before this DLL installed its hooks.
		AdoptMostRecentDirectCommandQueue(pSwapChain);

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

		{
			PerformanceMetrics::ScopedTimer maintenanceTimer(
				PerformanceMetrics::Timing::PresentShaderMaintenance,
				16);
			ProcessPendingRebuilds(); // <-- add here, before gInitialized check and before ImGui
			ApplyShaderTargetPSOs();
		}

		if (gOverlayRenderingDisabled)
			return CallOriginalPresent();

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

			if (!ImGui_ImplWin32_Init(desc.OutputWindow))
			{
				ShaderInjectorIO::WriteToLogFileError("HookD3D12->HandlePresentD3D12: ImGui Win32 backend initialization failed; overlay disabled");
				ImGui::DestroyContext();
				gOverlayRenderingDisabled = true;
				return CallOriginalPresent();
			}

			const bool imguiDX12Initialized = ImGui_ImplDX12_Init(gDevice, gBufferCount,
				desc.BufferDesc.Format, //test: d3d12 desc buffdesc format | format is 24
				gHeapSRV,
				gHeapSRV->GetCPUDescriptorHandleForHeapStart(),
				gHeapSRV->GetGPUDescriptorHandleForHeapStart()); //this also seemingly initalizes fine

			if (!imguiDX12Initialized)
			{
				ShaderInjectorIO::WriteToLogFileError("HookD3D12->HandlePresentD3D12: ImGui D3D12 backend initialization failed; overlay disabled");
				ImGui_ImplWin32_Shutdown();
				ImGui::DestroyContext();
				gOverlayRenderingDisabled = true;
				return CallOriginalPresent();
			}

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
		InstallDeferredRenderPassHooks(gDevice);
		RenderDocIntegration::PollCaptureStatus();


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

		// Protect only the first overlay submission from overlapping an active PSO creation call.
		// Once the overlay has submitted successfully, normal runtime pipeline creation must not
		// make the menu disappear or wait for the broader shader-rebuild quiet period.
		if (gOverlaySubmissionCount == 0 &&
			gActivePipelineActivityCount.load(std::memory_order_acquire) != 0)
		{
			if (!gLoggedOverlayPipelineActivityDelay)
			{
				ShaderInjectorIO::WriteToLogFile(StringHelper::Format(
					"HookD3D12->HandlePresentD3D12: active game pipeline call; delaying first overlay GPU submission activePipelineCalls=%u",
					static_cast<unsigned int>(gActivePipelineActivityCount.load(std::memory_order_acquire))));
				gLoggedOverlayPipelineActivityDelay = true;
			}

			return CallOriginalPresent();
		}

		if (gLoggedOverlayPipelineActivityDelay)
		{
			ShaderInjectorIO::WriteToLogFile(
				"HookD3D12->HandlePresentD3D12: active game pipeline call completed; first overlay GPU submission resumed");
			gLoggedOverlayPipelineActivityDelay = false;
		}

		if (!gShutdown) 
		{
			if (!gOverlayDeviceObjectsCreated)
			{
				gInsideOverlayResourceCreation = true;
				const bool deviceObjectsCreated = ImGui_ImplDX12_CreateDeviceObjects();
				gInsideOverlayResourceCreation = false;

				if (!deviceObjectsCreated)
				{
					const HRESULT removedReason = gDevice ? gDevice->GetDeviceRemovedReason() : E_POINTER;
					ShaderInjectorIO::WriteToLogFileError(
						"HookD3D12->HandlePresentD3D12: ImGui device-object creation failed; overlay disabled deviceRemovedReason=" +
						StringHelper::FormatHRESULT(removedReason));
					gOverlayRenderingDisabled = true;
					return CallOriginalPresent();
				}

				gOverlayDeviceObjectsCreated = true;
			}

			// Render ImGui
			//Apply MenuScale before NewFrame so font and layout metrics update together on the
			//frame after the user edits the live scale control.
			ShaderInjectorGUI::UI_ApplyStyle();
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

			// Each allocator can only be reset after the GPU has completed the overlay
			// submission that used that specific back buffer.
			bool canRender = true;

			if (!gOverlayFence || !gFenceEvent) 
			{
				// Missing synchronization objects, skip waiting
			}
			else if (ctx.fenceValue != 0 && gOverlayFence->GetCompletedValue() < ctx.fenceValue)
			{
				HRESULT hr = gOverlayFence->SetEventOnCompletion(ctx.fenceValue, gFenceEvent);

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
			hr = gCommandList->Close();

			if (FAILED(hr))
			{
				LogOverlayDeviceFailure("OverlayCommandListClose", hr);
				gOverlayRenderingDisabled = true;
				return CallOriginalPresent();
			}

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
					const UINT64 submittedFenceValue = ++gOverlayFenceValue;
					HRESULT hr = gCommandQueue->Signal(gOverlayFence, submittedFenceValue);

					if (SUCCEEDED(hr))
					{
						ctx.fenceValue = submittedFenceValue;
						++gOverlaySubmissionCount;

						if (gOverlaySubmissionCount == 1)
						{
							ShaderInjectorIO::WriteToLogFile(StringHelper::Format(
								"HookD3D12->HandlePresentD3D12: first overlay submission queue=%p backBuffer=%u fence=%llu",
								gCommandQueue,
								frameIdx,
								static_cast<unsigned long long>(submittedFenceValue)));
						}
					}
					else
					{
						LogOverlayDeviceFailure("OverlayFenceSignal", hr);
						gOverlayRenderingDisabled = true;
					}
				}
			}
		}

		return CallOriginalPresent();
	}

	HRESULT STDMETHODCALLTYPE Handle_PresentD3D12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags)
	{
		// RTSS may call the process-wide MinHook detour while our private vtable
		// wrapper is forwarding to it. Continue through MinHook's trampoline in
		// that case so the overlay chain executes exactly once.
		if (gInsideSwapChainCompatibilityCall && Original_PresentD3D12)
			return Original_PresentD3D12(pSwapChain, SyncInterval, Flags);

		if (!gRuntimeReady.load(std::memory_order_acquire))
			return Original_PresentD3D12
				? Original_PresentD3D12(pSwapChain, SyncInterval, Flags)
				: E_POINTER;

		return HandlePresentD3D12(pSwapChain, SyncInterval, Flags, nullptr, false);
	}

	HRESULT STDMETHODCALLTYPE Handle_Present1D3D12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pParams)
	{
		if (gInsideSwapChainCompatibilityCall && Original_Present1D3D12)
			return Original_Present1D3D12(pSwapChain, SyncInterval, Flags, pParams);

		if (!gRuntimeReady.load(std::memory_order_acquire))
			return Original_Present1D3D12
				? Original_Present1D3D12(pSwapChain, SyncInterval, Flags, pParams)
				: E_POINTER;

		return HandlePresentD3D12(pSwapChain, SyncInterval, Flags, pParams, true);
	}

	HRESULT STDMETHODCALLTYPE Handle_RTSSCompatibilityPresent(
		IDXGISwapChain3* pSwapChain,
		UINT SyncInterval,
		UINT Flags)
	{
		FunctionPresentD3D12 downstreamPresent = nullptr;
		{
			std::lock_guard<std::mutex> lock(gRTSSCompatibilityMutex);
			downstreamPresent = gRTSSOriginalPresent;
		}

		if (!downstreamPresent)
			return Original_PresentD3D12
				? Original_PresentD3D12(pSwapChain, SyncInterval, Flags)
				: E_POINTER;

		if (!gRuntimeReady.load(std::memory_order_acquire))
			return downstreamPresent(pSwapChain, SyncInterval, Flags);

		ScopedSwapChainCompatibilityCall compatibilityScope;
		return HandlePresentD3D12(
			pSwapChain,
			SyncInterval,
			Flags,
			nullptr,
			false,
			downstreamPresent,
			nullptr);
	}

	HRESULT STDMETHODCALLTYPE Handle_RTSSCompatibilityPresent1(
		IDXGISwapChain3* pSwapChain,
		UINT SyncInterval,
		UINT Flags,
		const DXGI_PRESENT_PARAMETERS* pParams)
	{
		FunctionPresent1D3D12 downstreamPresent1 = nullptr;
		{
			std::lock_guard<std::mutex> lock(gRTSSCompatibilityMutex);
			downstreamPresent1 = gRTSSOriginalPresent1;
		}

		if (!downstreamPresent1)
			return Original_Present1D3D12
				? Original_Present1D3D12(pSwapChain, SyncInterval, Flags, pParams)
				: E_POINTER;

		if (!gRuntimeReady.load(std::memory_order_acquire))
			return downstreamPresent1(pSwapChain, SyncInterval, Flags, pParams);

		ScopedSwapChainCompatibilityCall compatibilityScope;
		return HandlePresentD3D12(
			pSwapChain,
			SyncInterval,
			Flags,
			pParams,
			true,
			nullptr,
			downstreamPresent1);
	}

	HRESULT STDMETHODCALLTYPE Hook_PresentD3D12(IDXGISwapChain3* swapChain, UINT syncInterval, UINT flags)
	{
		return Handle_PresentD3D12(swapChain, syncInterval, flags);
	}

	HRESULT STDMETHODCALLTYPE Hook_Present1D3D12(IDXGISwapChain3* swapChain, UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters)
	{
		return Handle_Present1D3D12(swapChain, syncInterval, flags, parameters);
	}

	HRESULT STDMETHODCALLTYPE Hook_RTSSCompatibilityPresent(IDXGISwapChain3* swapChain, UINT syncInterval, UINT flags)
	{
		return Handle_RTSSCompatibilityPresent(swapChain, syncInterval, flags);
	}

	HRESULT STDMETHODCALLTYPE Hook_RTSSCompatibilityPresent1(IDXGISwapChain3* swapChain, UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters)
	{
		return Handle_RTSSCompatibilityPresent1(swapChain, syncInterval, flags, parameters);
	}
}
