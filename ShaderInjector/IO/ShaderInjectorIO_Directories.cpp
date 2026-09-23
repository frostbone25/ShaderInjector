#include "ShaderInjectorIO.h"

namespace ShaderInjectorIO
{
	//keep application paths derived from the executable so installs can live in any game folder.

	std::string GetGameDirectory()
	{
		return DirectoryFromPath(GetCurrentExecutablePath());
	}

	std::string GetShaderInjectorDirectory()
	{
		return JoinPath(GetGameDirectory(), "ShaderInjector");
	}

	std::string GetInternalDirectory()
	{
		return JoinPath(GetShaderInjectorDirectory(), "Internal");
	}

	std::string GetLogsDirectory()
	{
		return JoinPath(GetShaderInjectorDirectory(), "Logs");
	}

	std::string GetLogFilePath()
	{
		return JoinPath(GetLogsDirectory(), "ShaderInjector" + extensionLOG);
	}

	std::string GetPreviousLogFilePath()
	{
		return JoinPath(GetLogsDirectory(), "ShaderLogPrevious" + extensionLOG);
	}

	std::string GetDumpsDirectory()
	{
		return JoinPath(GetShaderInjectorDirectory(), "Dumps");
	}

	std::string GetUncapturedPSODirectory()
	{
		return JoinPath(GetShaderInjectorDirectory(), "UncapturedPSOs");
	}

	std::string GetToolsDirectory()
	{
		return JoinPath(GetShaderInjectorDirectory(), "Tools");
	}

	std::string GetToolPathDXC()
	{
		#if defined(_WIN32)
			return JoinPath(GetToolsDirectory(), "dxc" + extensionEXE);
		#else
			return JoinPath(GetToolsDirectory(), "dxc");
		#endif
	}

	std::string GetToolPathDXCompiler()
	{
		return JoinPath(GetToolsDirectory(), "dxcompiler.dll");
	}

	std::string GetShaderTargetsDirectory()
	{
		return JoinPath(GetShaderInjectorDirectory(), "ShaderTargets");
	}

	std::string GetRenderPassesDirectory()
	{
		return JoinPath(GetShaderInjectorDirectory(), "RenderPasses");
	}

	std::string GetShaderResourcesDirectory()
	{
		return JoinPath(GetShaderInjectorDirectory(), "ShaderResources");
	}

	std::string GetModifiedShadersDirectory()
	{
		return JoinPath(GetShaderInjectorDirectory(), "ModifiedShaders");
	}

	std::string GetModifiedShadersIncludesDirectory()
	{
		return JoinPath(GetModifiedShadersDirectory(), "Includes");
	}

	std::string GetShaderConfigurationsPath()
	{
		return JoinPath(GetModifiedShadersDirectory(), "ShaderConfigurations" + extensionJSON);
	}

	std::string GetInjectorSettingsPath()
	{
		return JoinPath(GetGameDirectory(), injectorSettingsName);
	}
}
