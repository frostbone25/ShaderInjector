//dsound_proxy.cpp

#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
// Windows Header Files
#include <windows.h>

#include <cstdio>
#include <mmsystem.h>
#include <dsound.h>

//custom
#include "dsound_proxy.h"

HMODULE g_realDsoundDll = nullptr;
static INIT_ONCE g_dsoundInitOnce = INIT_ONCE_STATIC_INIT;

using DirectSoundCaptureCreateFunction = HRESULT(WINAPI*)(LPCGUID, LPDIRECTSOUNDCAPTURE*, LPUNKNOWN);
using DirectSoundCaptureCreate8Function = HRESULT(WINAPI*)(LPCGUID, LPDIRECTSOUNDCAPTURE8*, LPUNKNOWN);
using DirectSoundCaptureEnumerateAFunction = HRESULT(WINAPI*)(LPDSENUMCALLBACKA, LPVOID);
using DirectSoundCaptureEnumerateWFunction = HRESULT(WINAPI*)(LPDSENUMCALLBACKW, LPVOID);
using DirectSoundCreateFunction = HRESULT(WINAPI*)(LPCGUID, LPDIRECTSOUND*, LPUNKNOWN);
using DirectSoundCreate8Function = HRESULT(WINAPI*)(LPCGUID, LPDIRECTSOUND8*, LPUNKNOWN);
using DirectSoundEnumerateAFunction = HRESULT(WINAPI*)(LPDSENUMCALLBACKA, LPVOID);
using DirectSoundEnumerateWFunction = HRESULT(WINAPI*)(LPDSENUMCALLBACKW, LPVOID);
using DirectSoundFullDuplexCreateFunction = HRESULT(WINAPI*)(
	LPCGUID,
	LPCGUID,
	LPCDSCBUFFERDESC,
	LPCDSBUFFERDESC,
	HWND,
	DWORD,
	LPDIRECTSOUNDFULLDUPLEX*,
	LPDIRECTSOUNDCAPTUREBUFFER8*,
	LPDIRECTSOUNDBUFFER8*,
	LPUNKNOWN);
using DllCanUnloadNowFunction = HRESULT(WINAPI*)();
using DllGetClassObjectFunction = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
using GetDeviceIDFunction = HRESULT(WINAPI*)(LPCGUID, LPGUID);

static DirectSoundCaptureCreateFunction gDirectSoundCaptureCreate = nullptr;
static DirectSoundCaptureCreate8Function gDirectSoundCaptureCreate8 = nullptr;
static DirectSoundCaptureEnumerateAFunction gDirectSoundCaptureEnumerateA = nullptr;
static DirectSoundCaptureEnumerateWFunction gDirectSoundCaptureEnumerateW = nullptr;
static DirectSoundCreateFunction gDirectSoundCreate = nullptr;
static DirectSoundCreate8Function gDirectSoundCreate8 = nullptr;
static DirectSoundEnumerateAFunction gDirectSoundEnumerateA = nullptr;
static DirectSoundEnumerateWFunction gDirectSoundEnumerateW = nullptr;
static DirectSoundFullDuplexCreateFunction gDirectSoundFullDuplexCreate = nullptr;
static DllCanUnloadNowFunction gDllCanUnloadNow = nullptr;
static DllGetClassObjectFunction gDllGetClassObject = nullptr;
static GetDeviceIDFunction gGetDeviceID = nullptr;

template <typename T>
static T LoadExport(const char* exportName)
{
	return reinterpret_cast<T>(GetProcAddress(g_realDsoundDll, exportName));
}

static BOOL CALLBACK LoadRealDsoundDllOnce(PINIT_ONCE, PVOID, PVOID*)
{
	char systemDirectory[MAX_PATH];
	GetSystemDirectoryA(systemDirectory, MAX_PATH);

	char dsoundPath[MAX_PATH];
	sprintf_s(dsoundPath, "%s\\dsound.dll", systemDirectory);

	g_realDsoundDll = LoadLibraryA(dsoundPath);

	if (g_realDsoundDll)
	{
		// Every exported proxy forwards to the system dsound.dll after our DLL is loaded.
		gDirectSoundCaptureCreate = LoadExport<DirectSoundCaptureCreateFunction>("DirectSoundCaptureCreate");
		gDirectSoundCaptureCreate8 = LoadExport<DirectSoundCaptureCreate8Function>("DirectSoundCaptureCreate8");
		gDirectSoundCaptureEnumerateA = LoadExport<DirectSoundCaptureEnumerateAFunction>("DirectSoundCaptureEnumerateA");
		gDirectSoundCaptureEnumerateW = LoadExport<DirectSoundCaptureEnumerateWFunction>("DirectSoundCaptureEnumerateW");
		gDirectSoundCreate = LoadExport<DirectSoundCreateFunction>("DirectSoundCreate");
		gDirectSoundCreate8 = LoadExport<DirectSoundCreate8Function>("DirectSoundCreate8");
		gDirectSoundEnumerateA = LoadExport<DirectSoundEnumerateAFunction>("DirectSoundEnumerateA");
		gDirectSoundEnumerateW = LoadExport<DirectSoundEnumerateWFunction>("DirectSoundEnumerateW");
		gDirectSoundFullDuplexCreate = LoadExport<DirectSoundFullDuplexCreateFunction>("DirectSoundFullDuplexCreate");
		gDllCanUnloadNow = LoadExport<DllCanUnloadNowFunction>("DllCanUnloadNow");
		gDllGetClassObject = LoadExport<DllGetClassObjectFunction>("DllGetClassObject");
		gGetDeviceID = LoadExport<GetDeviceIDFunction>("GetDeviceID");
	}

	return TRUE;
}

bool EnsureRealDsoundDllLoaded()
{
	InitOnceExecuteOnce(&g_dsoundInitOnce, LoadRealDsoundDllOnce, nullptr, nullptr);
	return g_realDsoundDll != nullptr;
}

bool LoadRealDsoundDll()
{
	return EnsureRealDsoundDllLoaded();
}

void FreeRealDsoundDll()
{
	if (g_realDsoundDll)
	{
		FreeLibrary(g_realDsoundDll);
		g_realDsoundDll = nullptr;
	}
}

extern "C"
{
	HRESULT WINAPI DirectSoundCaptureCreate(LPCGUID pcGuidDevice, LPDIRECTSOUNDCAPTURE* ppDSC, LPUNKNOWN pUnkOuter)
	{
		EnsureRealDsoundDllLoaded();
		return gDirectSoundCaptureCreate ? gDirectSoundCaptureCreate(pcGuidDevice, ppDSC, pUnkOuter) : E_FAIL;
	}

	HRESULT WINAPI DirectSoundCaptureCreate8(LPCGUID pcGuidDevice, LPDIRECTSOUNDCAPTURE8* ppDSC8, LPUNKNOWN pUnkOuter)
	{
		EnsureRealDsoundDllLoaded();
		return gDirectSoundCaptureCreate8 ? gDirectSoundCaptureCreate8(pcGuidDevice, ppDSC8, pUnkOuter) : E_FAIL;
	}

	HRESULT WINAPI DirectSoundCaptureEnumerateA(LPDSENUMCALLBACKA pDSEnumCallback, LPVOID pContext)
	{
		EnsureRealDsoundDllLoaded();
		return gDirectSoundCaptureEnumerateA ? gDirectSoundCaptureEnumerateA(pDSEnumCallback, pContext) : E_FAIL;
	}

	HRESULT WINAPI DirectSoundCaptureEnumerateW(LPDSENUMCALLBACKW pDSEnumCallback, LPVOID pContext)
	{
		EnsureRealDsoundDllLoaded();
		return gDirectSoundCaptureEnumerateW ? gDirectSoundCaptureEnumerateW(pDSEnumCallback, pContext) : E_FAIL;
	}

	HRESULT WINAPI DirectSoundCreate(LPCGUID pcGuidDevice, LPDIRECTSOUND* ppDS, LPUNKNOWN pUnkOuter)
	{
		EnsureRealDsoundDllLoaded();
		return gDirectSoundCreate ? gDirectSoundCreate(pcGuidDevice, ppDS, pUnkOuter) : E_FAIL;
	}

	HRESULT WINAPI DirectSoundCreate8(LPCGUID pcGuidDevice, LPDIRECTSOUND8* ppDS8, LPUNKNOWN pUnkOuter)
	{
		EnsureRealDsoundDllLoaded();
		return gDirectSoundCreate8 ? gDirectSoundCreate8(pcGuidDevice, ppDS8, pUnkOuter) : E_FAIL;
	}

	HRESULT WINAPI DirectSoundEnumerateA(LPDSENUMCALLBACKA pDSEnumCallback, LPVOID pContext)
	{
		EnsureRealDsoundDllLoaded();
		return gDirectSoundEnumerateA ? gDirectSoundEnumerateA(pDSEnumCallback, pContext) : E_FAIL;
	}

	HRESULT WINAPI DirectSoundEnumerateW(LPDSENUMCALLBACKW pDSEnumCallback, LPVOID pContext)
	{
		EnsureRealDsoundDllLoaded();
		return gDirectSoundEnumerateW ? gDirectSoundEnumerateW(pDSEnumCallback, pContext) : E_FAIL;
	}

	HRESULT WINAPI DirectSoundFullDuplexCreate(
		LPCGUID pcGuidCaptureDevice,
		LPCGUID pcGuidRenderDevice,
		LPCDSCBUFFERDESC pcDSCBufferDesc,
		LPCDSBUFFERDESC pcDSBufferDesc,
		HWND hWnd,
		DWORD dwLevel,
		LPDIRECTSOUNDFULLDUPLEX* ppDSFD,
		LPDIRECTSOUNDCAPTUREBUFFER8* ppDSCBuffer8,
		LPDIRECTSOUNDBUFFER8* ppDSBuffer8,
		LPUNKNOWN pUnkOuter)
	{
		EnsureRealDsoundDllLoaded();
		return gDirectSoundFullDuplexCreate
			? gDirectSoundFullDuplexCreate(
				pcGuidCaptureDevice,
				pcGuidRenderDevice,
				pcDSCBufferDesc,
				pcDSBufferDesc,
				hWnd,
				dwLevel,
				ppDSFD,
				ppDSCBuffer8,
				ppDSBuffer8,
				pUnkOuter)
			: E_FAIL;
	}

	HRESULT WINAPI DllCanUnloadNow()
	{
		EnsureRealDsoundDllLoaded();
		return gDllCanUnloadNow ? gDllCanUnloadNow() : S_FALSE;
	}

	HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
	{
		EnsureRealDsoundDllLoaded();
		return gDllGetClassObject ? gDllGetClassObject(rclsid, riid, ppv) : CLASS_E_CLASSNOTAVAILABLE;
	}

	HRESULT WINAPI GetDeviceID(LPCGUID pGuidSrc, LPGUID pGuidDest)
	{
		EnsureRealDsoundDllLoaded();
		return gGetDeviceID ? gGetDeviceID(pGuidSrc, pGuidDest) : E_FAIL;
	}
}
