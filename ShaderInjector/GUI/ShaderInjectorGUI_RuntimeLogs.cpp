//ShaderInjectorGUI.cpp
#include "ShaderInjectorGUI.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <mutex>
#include <vector>

//3RD Party
#include "imgui.h"

//custom
#include "HookD3D12.h"
#include "ShaderInjectorIO.h"
#include "Hash.h"
#include "Globals.h"
#include "DatabaseModifiedShaders.h"
#include "DatabaseRenderPasses.h"
#include "DatabaseShaderConfigurations.h"
#include "ModifiedShaderCreation.h"
#include "RenderDocIntegration.h"
#include "RenderPassRuntime.h"
#include "ShaderAutomaticDiscovery.h"
#include "StringHelper.h"
#include "ShaderInjectorGUITooltips.h"
#include "Keycodes.h"
#include "ShaderInjectorVersion.h"

namespace ShaderInjectorGUI
{
	static std::mutex gRuntimeLogMutex;

	void WriteToRuntimeLog(std::string text)
	{
		{
			std::lock_guard<std::mutex> lock(gRuntimeLogMutex);
			runtimeLogText += "\n";
			runtimeLogText += text;
		}

		ShaderInjectorIO::WriteToLogFile(text);
	}

	void WriteToRuntimeLogError(std::string text)
	{
		WriteToRuntimeLog("[ERROR] " + text);
	}

	void WriteToRuntimeLogSuccess(std::string text)
	{
		WriteToRuntimeLog("[SUCCESS] " + text);
	}

	void WriteToRuntimeLogWarning(std::string text)
	{
		WriteToRuntimeLog("[WARNING] " + text);
	}

	void ClearRuntimeLog()
	{
		std::lock_guard<std::mutex> lock(gRuntimeLogMutex);
		runtimeLogText = "";
	}

	std::string GetRuntimeLogSnapshot()
	{
		std::lock_guard<std::mutex> lock(gRuntimeLogMutex);
		return runtimeLogText;
	}
}
