#include <windows.h>

#include "MinHook.h"

#include "Globals.h"
#include "HookD3D12.h"
#include "Hooks.h"
#include "dsound_proxy.h"
#include "IO/ShaderInjectorIO.h"
#include "StringHelper.h"
#include "ModifiedShader/DatabaseModifiedShaders.h"
#include "ShaderTarget/DatabaseShaderTargets.h"
#include "RenderPass/DatabaseRenderPasses.h"
#include "RenderDoc/RenderDocIntegration.h"
#include "ShaderInjectorVersion.h"
#include "ShaderInjectorInternalResources.h"

//dllmain starts this worker so hook setup can wait for the game without holding the loader lock.
static DWORD WINAPI OnAttachDLL(LPVOID)
{
	//load the real sound library first so forwarded exports are ready during setup.
	LoadRealDsoundDll();

	//keep the previous run's log available when startup fails or the game crashes.
	ShaderInjectorIO::RotateLogFiles();

	//read settings before initializing the optional RenderDoc bridge.
	//unless AutoAttach is explicitly enabled, initialization only detects an already injected RenderDoc module.
	ShaderInjectorIO::ReadInjectorSettings();
	ShaderInjectorIO::WriteToLogFile("dllmain->OnAttachDLL: Shader Injector version " + std::string(SHADER_INJECTOR_VERSION_STRING));

	RenderDocIntegration::Initialize();

	//initialize MinHook before the delay so early swap chains can be observed.
	//the rendering hooks are installed later, after packages and internal shaders are ready.
	MH_STATUS minHookStatus = MH_Initialize();

	if (minHookStatus != MH_OK)
	{
		ShaderInjectorIO::WriteToLogFileError(StringHelper::Format("dllmain->OnAttachDLL: MH_Initialize failed: %s", MH_StatusToString(minHookStatus)));
		return 0;
	}

	ShaderInjectorIO::WriteToLogFile("dllmain->OnAttachDLL: MinHook initialized.");

	Hooks::PrepareSwapChainCapture();

	//give the game's D3D12 startup time to settle before installing the remaining hooks.
	Sleep(5000);

	//prepare the injector's folders, settings files, and bundled shader sources.
	ShaderInjectorIO::Initialize();

	//record machine, executable, and display details for diagnosing startup issues.
	ShaderInjectorIO::LogProcessAndSystemInfo();

	//load shader packages before matching targets against game pipelines.
	DatabaseModifiedShaders::RefreshModifiedShaders();

	//warm-cache games can bind pipelines immediately after hook installation.
	//publish targets now so those first binds have the strongest saved identity available.
	HookD3D12::RefreshLoadedShaderTargets();

	//load the graph definitions before a hooked draw can trigger an injected pass.
	DatabaseRenderPasses::RefreshRenderPasses();

	ShaderInjectorIO::WriteToLogFile("dllmain->OnAttachDLL: startup worker initialized.");

	//prepare, compile, and load the internal marker/null shaders before the D3D12 hooks can observe any game pipeline state.
	ShaderInjectorInternalResources::Initialize();

	//the device hook needs the D3D12 module that the game has already loaded.
	HMODULE d3d12Module = GetModuleHandleA("d3d12.dll");

	if (!d3d12Module)
	{
		ShaderInjectorIO::WriteToLogFileError("dllmain->OnAttachDLL: d3d12.dll handle not found!");
		return 0;
	}

	ShaderInjectorIO::WriteToLogFile(StringHelper::Format("dllmain->OnAttachDLL: d3d12.dll = %p", d3d12Module));

	//install the device entry hook, then publish the runtime-ready flag after setup completes.
	HookD3D12::InstallD3D12CreateDeviceHook(d3d12Module);

	Hooks::Initialize();
	HookD3D12::SetRuntimeReady(true);

	ShaderInjectorIO::WriteToLogFile("dllmain->OnAttachDLL: hook initialization complete.");

	return 0;
}

//keep dll entry work small; the worker handles setup after the loader lock is released.
BOOL APIENTRY DllMain(HMODULE dllModule, DWORD callReason, LPVOID)
{
	switch (callReason)
	{
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(dllModule);
		Globals::mainModule = dllModule;

		//the case scope keeps the thread handle local to process attachment.
		{
			HANDLE initializationThread = CreateThread(nullptr, 0, OnAttachDLL, nullptr, 0, nullptr);
			if (initializationThread)
				CloseHandle(initializationThread);
			else
				MessageBoxA(nullptr, "[ERROR] dllmain->DllMain: Failed to create hook thread!", "Shader Injector", MB_OK);
		}
		break;

	case DLL_PROCESS_DETACH:
		//release the proxy and remove hooks while the process detaches.
		FreeRealDsoundDll();
		MH_DisableHook(MH_ALL_HOOKS);
		MH_RemoveHook(MH_ALL_HOOKS);
		MH_Uninitialize();
		break;
	}

	return TRUE;
}
