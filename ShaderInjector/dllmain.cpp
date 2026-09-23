//dllmain.cpp
#include <windows.h>

//3RD PARTY
#include "MinHook.h"

//custom
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

//||||||||||||||||||||||||||||||| ON ATTACH |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| ON ATTACH |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| ON ATTACH |||||||||||||||||||||||||||||||

//thread entry: initialize MinHook and start hook setup
static DWORD WINAPI OnAttachDLL(LPVOID)
{
	//IMPORTANT NOTE 1: The game's D3D12 device already exists before our proxy loads.

	//since we proxy dsound dll, we should go ahead and load the real deal now
	LoadRealDsoundDll();

	//||||||||||||||||||||||||||||||| LOGS |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| LOGS |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| LOGS |||||||||||||||||||||||||||||||

	//since we are currently starting with the new process again, check for a "current" log file
	//it will become the "previous" log file to better retain any information about potential crashing/issues
	ShaderInjectorIO::RotateLogFiles();

	//||||||||||||||||||||||||||||||| RENDERDOC |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| RENDERDOC |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| RENDERDOC |||||||||||||||||||||||||||||||

	//read settings before initializing the optional RenderDoc bridge.
	//unless AutoAttach is explicitly enabled, initialization only detects an already injected RenderDoc module.
	ShaderInjectorIO::ReadInjectorSettings();
	ShaderInjectorIO::WriteToLogFile("dllmain->OnAttachDLL: Shader Injector version " + std::string(SHADER_INJECTOR_VERSION_STRING));

	RenderDocIntegration::Initialize();

	//||||||||||||||||||||||||||||||| MINHOOK |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| MINHOOK |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| MINHOOK |||||||||||||||||||||||||||||||

	//FIX: initialize MinHook before the startup delay so an OptiScaler factory can be captured before it creates its frame-generation swap chain.
	//no shader or rendering callbacks are enabled by this step alone.
	MH_STATUS minhookStatus = MH_Initialize();

	if (minhookStatus != MH_OK)
	{
		ShaderInjectorIO::WriteToLogFileError(StringHelper::Format("dllmain->OnAttachDLL: MH_Initialize failed: %s", MH_StatusToString(minhookStatus)));
		return 0;
	}

	ShaderInjectorIO::WriteToLogFile("dllmain->OnAttachDLL: minhook initalized!");

	//||||||||||||||||||||||||||||||| MINHOOK |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| MINHOOK |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| MINHOOK |||||||||||||||||||||||||||||||

	Hooks::PrepareSwapChainCapture();

	//NOTE TO SELF: not a fan of this, even though it helps...
	//this is just to ensure we don't get crazy crashes or timing issues with d3d12 because im sick and tired of crashing
	Sleep(5000);

	//||||||||||||||||||||||||||||||| INITALIZE IO |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| INITALIZE IO |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| INITALIZE IO |||||||||||||||||||||||||||||||

	//initalize IO operations (folders, files, internal shader files)
	ShaderInjectorIO::Initialize();

	//record machine, executable, and display diagnostics for user bug reports
	ShaderInjectorIO::LogProcessAndSystemInfo();

	//collect modified shaders stored in "ShaderInjector/ModifiedShaders"
	DatabaseModifiedShaders::RefreshModifiedShaders();

	//load shader targets before installing the D3D12 hooks
	//warm-cache games can bind important opaque PSOs immediately after hook installation
	//publishing the targets here lets those first observations use the strongest available hash/template identity instead of relying only on a later recovery scan.
	HookD3D12::RefreshLoadedShaderTargets();

	//collect custom render-pass timing and resource-tracking definitions
	DatabaseRenderPasses::RefreshRenderPasses();

	//IMPORTANT NOTE 2: We are able to create and run this thread, so this does execute and work!
	//NOTE 1: keep this comment around for sanity check please!
	//NOTE 2: because this is on a seperate thread, popping a message box will not freeze the application!
	ShaderInjectorIO::WriteToLogFile("dllmain->OnAttachDLL: dsound thread initalized!");

	//prepare, compile, and load the internal marker/null shaders before the D3D12 hooks can observe any game pipeline state.
	ShaderInjectorInternalResources::Initialize();

	//||||||||||||||||||||||||||||||| D3D12 CHECK |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| D3D12 CHECK |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| D3D12 CHECK |||||||||||||||||||||||||||||||

	//NOTE: sanity check, ensure that we have d3d12.dll
	HMODULE d3d12 = GetModuleHandleA("d3d12.dll");

	if (!d3d12)
	{
		ShaderInjectorIO::WriteToLogFileError("dllmain->OnAttachDLL: d3d12.dll handle not found!");
		return 0;
	}

	//IMPORTANT NOTE 4: We can infact find the d3d12.dll and get a handle on it!
	//NOTE: keep this comment around for sanity check please!
	ShaderInjectorIO::WriteToLogFile(StringHelper::Format("dllmain->OnAttachDLL: d3d12.dll = %p", d3d12));

	//||||||||||||||||||||||||||||||| D3D12 HOOKS |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| D3D12 HOOKS |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| D3D12 HOOKS |||||||||||||||||||||||||||||||

	//this is where the real madness begins...
	//hook into d3d12 device creation and start hooking into many of it's calls
	HookD3D12::InstallD3D12CreateDeviceHook(d3d12);

	Hooks::Initialize();
	HookD3D12::SetRuntimeReady(true);

	//log to make sure we're done hooking!
	ShaderInjectorIO::WriteToLogFile("dllmain->OnAttachDLL: hook initialization complete.");

	return 0;
}

//||||||||||||||||||||||||||||||| DLL MAIN |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| DLL MAIN |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| DLL MAIN |||||||||||||||||||||||||||||||
//ref - https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain

//hModule: handle to DLL module
//reason: reason for calling function
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
	//perform actions based on the reason for calling.
	switch (reason)
	{
		//||||||||||||||||||||||||||||||| DLL ATTACHMENT |||||||||||||||||||||||||||||||
		//||||||||||||||||||||||||||||||| DLL ATTACHMENT |||||||||||||||||||||||||||||||
		//||||||||||||||||||||||||||||||| DLL ATTACHMENT |||||||||||||||||||||||||||||||
		//DLL is being loaded into the virtual address space of the current process as a result of the process starting up or as a result of a call to LoadLibrary.
		case DLL_PROCESS_ATTACH:
			DisableThreadLibraryCalls(hModule);

			Globals::mainModule = hModule;

			//FIX: surrounded most of this with brackets, as we get a wierd scoping issue involving the thread
			//Error	C2360: initialization of 'hookThread' is skipped by 'case' label
			{
				//it's important that we create a seperate thread for hooking, otherwise we block the application from loading at all!
				HANDLE hookThread = CreateThread(
					nullptr,
					0,
					OnAttachDLL,
					nullptr,
					0,
					nullptr);

				if (hookThread)
				{
					CloseHandle(hookThread);

					//NOTE 1: keep this comment around just in case we hit headaches later, sanity check to verify if we are even creating a hook thread
					//NOTE 2: popping a message box here will freeze the application!
					//MessageBoxA(nullptr, "dllmain->DllMain: Created hook thread!", "Shader Injector", MB_OK);
				}
				else
				{
					//NOTE 1: keep this comment around just in case we hit headaches later, sanity check to verify when we aren't able to create a hook thread
					//NOTE 2: popping a message box here will freeze the application!
					MessageBoxA(nullptr, "[ERROR] dllmain->DllMain: Failed to create hook thread!", "Shader Injector", MB_OK);
				}
	

				//NOTE 1: keep this comment around just in case we hit headaches later, sanity check to verify if the dll is even getting attached
				//NOTE 2: popping a message box here will freeze the application!
				//MessageBoxA(nullptr, "dllmain->DllMain: dsound attached!", "Shader Injector", MB_OK);
			}

			break;

		//||||||||||||||||||||||||||||||| DLL DETATCHMENT |||||||||||||||||||||||||||||||
		//||||||||||||||||||||||||||||||| DLL DETATCHMENT |||||||||||||||||||||||||||||||
		//||||||||||||||||||||||||||||||| DLL DETATCHMENT |||||||||||||||||||||||||||||||
		//The DLL is being unloaded from the virtual address space of the calling process because it was loaded unsuccessfully or the reference count has reached zero (the processes has either terminated or called FreeLibrary one time for each time it called LoadLibrary).
		case DLL_PROCESS_DETACH:

			FreeRealDsoundDll();

			//cleanup minhook
			MH_DisableHook(MH_ALL_HOOKS);
			MH_RemoveHook(MH_ALL_HOOKS);
			MH_Uninitialize();

			break;
	}

	return TRUE; //successful DLL_PROCESS_ATTACH.
}
