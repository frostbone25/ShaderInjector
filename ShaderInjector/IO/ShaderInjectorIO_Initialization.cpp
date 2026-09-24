#include "ShaderInjectorIO.h"

namespace ShaderInjectorIO
{
	//create the injector's working folders after loading settings, so startup logs reflect the configured behavior.
	bool Initialize()
	{
		WriteToLogFile("ShaderInjectorIO->Initialize: Initializing...");

		//resolve paths once so every startup check uses the same game-relative locations.
		const std::string shaderInjectorDirectory = GetShaderInjectorDirectory();
		const std::string internalDirectory = GetInternalDirectory();
		const std::string logsDirectory = GetLogsDirectory();
		const std::string toolsDirectory = GetToolsDirectory();
		const std::string dumpDirectory = GetDumpsDirectory();
		const std::string uncapturedPSODirectory = GetUncapturedPSODirectory();
		const std::string shaderTargetsDirectory = GetShaderTargetsDirectory();
		const std::string renderPassesDirectory = GetRenderPassesDirectory();
		const std::string shaderResourcesDirectory = GetShaderResourcesDirectory();
		const std::string modifiedShadersDirectory = GetModifiedShadersDirectory();
		const std::string modifiedShadersIncludesDirectory = GetModifiedShadersIncludesDirectory();
		const std::string injectorSettingsPath = GetInjectorSettingsPath();

		//load settings before creating folders so the rest of startup sees the user's configured behavior.
		const bool injectorSettingsReadResult = ReadInjectorSettings();

		if (!injectorSettingsReadResult && !FileExists(injectorSettingsPath))
			CreateInjectorSettings();

		if (!DirectoryExists(shaderInjectorDirectory))
		{
			DirectoryCreate(shaderInjectorDirectory);
			WriteToLogFileWarning("ShaderInjectorIO->Initialize: " + shaderInjectorDirectory + " did not exist; created it.");
		}

		if (!DirectoryExists(internalDirectory))
		{
			DirectoryCreate(internalDirectory);
			WriteToLogFile("ShaderInjectorIO->Initialize: " + internalDirectory + " did not exist; created it.");
		}

		if (!DirectoryExists(logsDirectory))
		{
			DirectoryCreate(logsDirectory);
			WriteToLogFile("ShaderInjectorIO->Initialize: " + logsDirectory + " did not exist; created it.");
		}

		if (!DirectoryExists(toolsDirectory))
		{
			DirectoryCreate(toolsDirectory);
			WriteToLogFileWarning("ShaderInjectorIO->Initialize: " + toolsDirectory + " did not exist; created it.");
		}

		if (!DirectoryExists(dumpDirectory))
		{
			DirectoryCreate(dumpDirectory);
			WriteToLogFile("ShaderInjectorIO->Initialize: " + dumpDirectory + " did not exist; created it.");
		}

		if (!DirectoryExists(uncapturedPSODirectory))
		{
			DirectoryCreate(uncapturedPSODirectory);
			WriteToLogFile("ShaderInjectorIO->Initialize: " + uncapturedPSODirectory + " did not exist; created it.");
		}

		if (!DirectoryExists(shaderTargetsDirectory))
		{
			DirectoryCreate(shaderTargetsDirectory);
			WriteToLogFile("ShaderInjectorIO->Initialize: " + shaderTargetsDirectory + " did not exist; created it.");
		}

		if (!DirectoryExists(renderPassesDirectory))
		{
			DirectoryCreate(renderPassesDirectory);
			WriteToLogFile("ShaderInjectorIO->Initialize: " + renderPassesDirectory + " did not exist; created it.");
		}

		if (!DirectoryExists(shaderResourcesDirectory))
		{
			DirectoryCreate(shaderResourcesDirectory);
			WriteToLogFile("ShaderInjectorIO->Initialize: " + shaderResourcesDirectory + " did not exist; created it.");
		}

		if (!DirectoryExists(modifiedShadersDirectory))
		{
			DirectoryCreate(modifiedShadersDirectory);
			WriteToLogFileWarning("ShaderInjectorIO->Initialize: " + modifiedShadersDirectory + " did not exist; created it.");
		}

		if (!DirectoryExists(modifiedShadersIncludesDirectory))
		{
			DirectoryCreate(modifiedShadersIncludesDirectory);
			WriteToLogFile("ShaderInjectorIO->Initialize: " + modifiedShadersIncludesDirectory + " did not exist; created it.");
		}

		return true;
	}
} //namespace ShaderInjectorIO
