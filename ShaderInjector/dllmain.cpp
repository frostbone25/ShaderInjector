//dllmain.cpp
#include <windows.h>

//3RD PARTY
#include "MinHook.h"

//custom
#include "Globals.h"
#include "HookD3D12.h"
#include "Hooks.h"
#include "dsound_proxy.h"
#include "ShaderInjectorIO.h"
#include "StringHelper.h"
#include "DatabaseModifiedShaders.h"
#include "RenderDocIntegration.h"
#include "SystemInfoLogger.h"

//||||||||||||||||||||||||||||||| ON ATTACH |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| ON ATTACH |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| ON ATTACH |||||||||||||||||||||||||||||||

//thread entry: initialize MinHook and start hook setup
static DWORD WINAPI OnAttachDLL(LPVOID)
{
	//IMPORTANT NOTE 1: The game's D3D12 device already exists before our proxy loads.

	LoadRealDsoundDll();

	//Read settings before initializing the optional RenderDoc bridge. Unless AutoAttach is
	//explicitly enabled, initialization only detects an already injected RenderDoc module.
	ShaderInjectorIO::RotateLogFiles();
	ShaderInjectorIO::ReadInjectorSettings();
	RenderDocIntegration::Initialize();

	//NOTE TO SELF: not a fan of this, even though it helps...
	//this is just to ensure we don't get crazy crashes or timing issues with d3d12 because im sick and tired of crashing
	Sleep(5000);

	//||||||||||||||||||||||||||||||| INITALIZE IO |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| INITALIZE IO |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| INITALIZE IO |||||||||||||||||||||||||||||||

	//initalize IO operations (folders, files, internal shader files)
	ShaderInjectorIO::Initialize();

	//record machine, executable, and display diagnostics for user bug reports
	SystemInfoLogger::LogProcessAndSystemInfo();

	//collect modified shaders stored in "ShaderInjector/ModifiedShaders"
	DatabaseModifiedShaders::RefreshModifiedShaders();

	//IMPORTANT NOTE 2: We are able to create and run this thread, so this does execute and work!
	//NOTE 1: keep this comment around for sanity check please!
	//NOTE 2: because this is on a seperate thread, popping a message box will not freeze the application!
	ShaderInjectorIO::WriteToLogFile("dllmain->OnAttachDLL: dsound thread initalized!");

	//||||||||||||||||||||||||||||||| NULL PIXEL SHADER |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| NULL PIXEL SHADER |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| NULL PIXEL SHADER |||||||||||||||||||||||||||||||
	//prepare our "internal" shader resources for an error pixel shader (red)
	//NOTE: I prefer at this point in time to just rewrite the shaders based off a code template in the codebase
	//then compile them right at the start, that way we start fresh, just in case the users for whatever reason tamper with them (or move/delete them)

	ShaderInjectorIO::WriteToLogFile("dllmain->OnAttachDLL: getting null shader...");

	//collect source and blob path
	std::string errorPixelShaderPath = ShaderInjectorIO::GetInternalNullPixelShaderSourceCodeFilePath();
	std::string errorPixelShaderBlobPath = ShaderInjectorIO::GetInternalNullPixelShaderBlobFilePath();

	//compile "error" pixel shader
	bool nullPixelShaderCompiled = ShaderInjectorIO::CompileSourceToDXILBlob(
		errorPixelShaderPath, //NOTE: this NEEDS to be an .hlsl source shader text file
		"ps_6_6", //target profile, for rebirth ps_6_6 is what I found in renderdoc
		"main", //name of the function within the shader to execute
		errorPixelShaderBlobPath); //output blob file path

	if (nullPixelShaderCompiled)
	{
		//NOTE: 'nullShaderBlobPath' this NEEDS to be a DXIL compiled binary file
		//'Globals::nullPixelShaderBlob' array to contain the data in memory
		bool nullShaderLoaded = ShaderInjectorIO::LoadDXILBlobFromDisk(errorPixelShaderBlobPath, Globals::nullPixelShaderBlob);

		if (!nullShaderLoaded || Globals::nullPixelShaderBlob.empty())
		{
			Globals::nullPixelShaderBlob.clear();
			ShaderInjectorIO::WriteToLogFileError("dllmain->OnAttachDLL: failed to load null pixel shader blob: " + errorPixelShaderBlobPath);
		}
	}
	else
	{
		Globals::nullPixelShaderBlob.clear();
		ShaderInjectorIO::WriteToLogFileError("dllmain->OnAttachDLL: failed to compile null pixel shader: " + errorPixelShaderPath);
	}

	ShaderInjectorIO::WriteToLogFile("dllmain->OnAttachDLL: globals::nullPixelShaderBlob size " + std::to_string(Globals::nullPixelShaderBlob.size()));

	//||||||||||||||||||||||||||||||| SELECTION MARKER PIXEL SHADER |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| SELECTION MARKER PIXEL SHADER |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| SELECTION MARKER PIXEL SHADER |||||||||||||||||||||||||||||||
	//prepare our "internal" shader resources for a marker pixel shader (blue)
	//NOTE: I prefer at this point in time to just rewrite the shaders based off a code template in the codebase
	//then compile them right at the start, that way we start fresh, just in case the users for whatever reason tamper with them (or move/delete them)

	std::string markerPixelShaderBlobPath = ShaderInjectorIO::GetInternalMarkerPixelShaderBlobFilePath();

	bool markerPixelLoaded = ShaderInjectorIO::LoadDXILBlobFromDisk(markerPixelShaderBlobPath, Globals::markerPixelShaderBlob);

	if (!markerPixelLoaded || Globals::markerPixelShaderBlob.empty())
	{
		Globals::markerPixelShaderBlob.clear();
		ShaderInjectorIO::WriteToLogFileError("dllmain->OnAttachDLL: failed to load marker pixel shader blob: " + markerPixelShaderBlobPath);
	}

	//||||||||||||||||||||||||||||||| SELECTION MARKER COMPUTE SHADER |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| SELECTION MARKER COMPUTE SHADER |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| SELECTION MARKER COMPUTE SHADER |||||||||||||||||||||||||||||||

	std::string markerComputeShaderBlobPath = ShaderInjectorIO::GetInternalMarkerComputeShaderBlobFilePath();

	bool markerComputeLoaded = ShaderInjectorIO::LoadDXILBlobFromDisk(markerComputeShaderBlobPath, Globals::markerComputeShaderBlob);

	if (!markerComputeLoaded || Globals::markerComputeShaderBlob.empty())
	{
		Globals::markerComputeShaderBlob.clear();
		ShaderInjectorIO::WriteToLogFileError("dllmain->OnAttachDLL: failed to load marker compute shader blob: " + markerComputeShaderBlobPath);
	}

	ShaderInjectorIO::WriteToLogFile("dllmain->OnAttachDLL: globals::markerPixelShaderBlob size " + std::to_string(Globals::markerPixelShaderBlob.size()));
	ShaderInjectorIO::WriteToLogFile("dllmain->OnAttachDLL: globals::markerComputeShaderBlob size " + std::to_string(Globals::markerComputeShaderBlob.size()));

	//||||||||||||||||||||||||||||||| INITALIZE MINHOOK |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| INITALIZE MINHOOK |||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||| INITALIZE MINHOOK |||||||||||||||||||||||||||||||

	MH_STATUS minhookStatus = MH_Initialize();

	if (minhookStatus != MH_OK)
	{
		ShaderInjectorIO::WriteToLogFileError(StringHelper::Format("dllmain->OnAttachDLL: MH_Initialize failed: %s", MH_StatusToString(minhookStatus)));
		return 0;
	}

	//IMPORTANT NOTE 3: We are able to create and initalize minhook, so this does work!
	//NOTE: keep this comment around for sanity check please!
	ShaderInjectorIO::WriteToLogFile("dllmain->OnAttachDLL: minhook initalized!");

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

	//this is where the real madness begins...
	//hook into d3d12 device creation and start hooking into many of it's calls
	HookD3D12::InstallD3D12CreateDeviceHook(d3d12);

	Hooks::Initialize();

	//log to make sure we're done hooking!
	ShaderInjectorIO::WriteToLogFile("dllmain->OnAttachDLL: hook initialization complete.");

	return 0;
}

//||||||||||||||||||||||||||||||| DLL MAIN |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| DLL MAIN |||||||||||||||||||||||||||||||
//||||||||||||||||||||||||||||||| DLL MAIN |||||||||||||||||||||||||||||||

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
