#include "ShaderInjectorIO.h"

#include <algorithm>
#include <sstream>
#include <string>

#include "inicpp.h"

#include "../Globals.h"
#include "../ShaderTarget/ShaderTarget.h"
#include "../StringHelper.h"

namespace ShaderInjectorIO
{
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| INJECTOR SETTINGS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| INJECTOR SETTINGS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| INJECTOR SETTINGS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//keep the serialized INI names in one place.
	//these values are part of the on-disk configuration format, so changing one should be an intentional compatibility change.
	static const char* const settingsSectionInjector = "InjectorSettings";
	static const char* const settingsSectionShaderCompiler = "ShaderCompiler";
	static const char* const settingsSectionRenderDoc = "RenderDoc";
	static const char* const settingsSectionLogging = "Logging";
	static const char* const settingsSectionShaderDiscovery = "ShaderDiscovery";

	static const char* const settingsNameOpenMenuKey = "OpenMenuKey";
	static const char* const settingsNameToggleInjectorKey = "ToggleInjectorKey";
	static const char* const settingsNameInjectorEnabled = "InjectorEnabled";
	static const char* const settingsNameMenuOpen = "MenuOpen";
	static const char* const settingsNameMenuScale = "MenuScale";

	static const char* const settingsNameVertexShaderModel = "VertexShaderModel";
	static const char* const settingsNameAutoDetectShaderModels = "AutoDetectShaderModels";
	static const char* const settingsNameHullShaderModel = "HullShaderModel";
	static const char* const settingsNameDomainShaderModel = "DomainShaderModel";
	static const char* const settingsNameGeometryShaderModel = "GeometryShaderModel";
	static const char* const settingsNamePixelShaderModel = "PixelShaderModel";
	static const char* const settingsNameComputeShaderModel = "ComputeShaderModel";

	static const char* const settingsNameEnabled = "Enabled";
	static const char* const settingsNameAutoAttach = "AutoAttach";

	static const char* const settingsNameDisableLogs = "DisableLogs";
	static const char* const settingsNameVerboseLog = "VerboseLog";
	static const char* const settingsNameLogModifiedShaderNames = "LogModifiedShaderNames";
	static const char* const settingsNamePerformanceTelemetry = "PerformanceTelemetry";

	static const char* const settingsNameMode = "Mode";
	static const char* const settingsNameWorkerThreads = "WorkerThreads";
	static const char* const settingsNameWorkerThreadPriority = "WorkerThreadPriority";
	static const char* const settingsNameFrameJobBudget = "FrameJobBudget";
	static const char* const settingsNamePendingAnalysisLimit = "PendingAnalysisLimit";
	static const char* const settingsNameQueuedShaderLimit = "QueuedShaderLimit";
	static const char* const settingsNameMinimumSimilarityScore = "MinimumSimilarityScore";
	static const char* const settingsNameSimilarityAmbiguityMargin = "SimilarityAmbiguityMargin";

	template<typename ValueType>
	ValueType ReadIniValueOrDefault(
		ini::IniFile& iniFile,
		const char* sectionName,
		const char* keyName,
		const ValueType& defaultValue)
	{
		try
		{
			return iniFile[sectionName][keyName].as<ValueType>();
		}
		catch (...)
		{
			return defaultValue;
		}
	}

	void PopulateInjectorSettings(ini::IniFile& injectorSettingsINI)
	{
		//injector settings
		injectorSettingsINI[settingsSectionInjector][settingsNameOpenMenuKey] = Globals::keyOpenShaderInjectorGUI;
		injectorSettingsINI[settingsSectionInjector][settingsNameToggleInjectorKey] = Globals::keyToggleShaderInjector;
		injectorSettingsINI[settingsSectionInjector][settingsNameInjectorEnabled] = Globals::gShaderInjectorEnabled;
		injectorSettingsINI[settingsSectionInjector][settingsNameMenuOpen] = Globals::gShowShaderInjectorGUI;
		injectorSettingsINI[settingsSectionInjector][settingsNameMenuScale] = static_cast<double>(Globals::gShaderInjectorGUIScale);

		//shader compiler
		injectorSettingsINI[settingsSectionShaderCompiler][settingsNameVertexShaderModel] = static_cast<int>(Globals::gVertexShaderModel);
		injectorSettingsINI[settingsSectionShaderCompiler][settingsNameAutoDetectShaderModels] = Globals::gAutoDetectShaderModels;
		injectorSettingsINI[settingsSectionShaderCompiler][settingsNameHullShaderModel] = static_cast<int>(Globals::gHullShaderModel);
		injectorSettingsINI[settingsSectionShaderCompiler][settingsNameDomainShaderModel] = static_cast<int>(Globals::gDomainShaderModel);
		injectorSettingsINI[settingsSectionShaderCompiler][settingsNameGeometryShaderModel] = static_cast<int>(Globals::gGeometryShaderModel);
		injectorSettingsINI[settingsSectionShaderCompiler][settingsNamePixelShaderModel] = static_cast<int>(Globals::gPixelShaderModel);
		injectorSettingsINI[settingsSectionShaderCompiler][settingsNameComputeShaderModel] = static_cast<int>(Globals::gComputeShaderModel);

		//render doc
		injectorSettingsINI[settingsSectionRenderDoc][settingsNameEnabled] = Globals::gRenderDocIntegrationEnabled;
		injectorSettingsINI[settingsSectionRenderDoc][settingsNameAutoAttach] = Globals::gRenderDocAutoAttachEnabled;

		//logging
		injectorSettingsINI[settingsSectionLogging][settingsNameDisableLogs] = Globals::gDisableLogs;
		injectorSettingsINI[settingsSectionLogging][settingsNameVerboseLog] = Globals::gVerboseLog;
		injectorSettingsINI[settingsSectionLogging][settingsNameLogModifiedShaderNames] = Globals::gLogModifiedShaderNames;
		injectorSettingsINI[settingsSectionLogging][settingsNamePerformanceTelemetry] = Globals::gPerformanceTelemetryEnabled;

		//shader discovery
		injectorSettingsINI[settingsSectionShaderDiscovery][settingsNameMode] = static_cast<int>(Globals::gShaderDiscoveryMode);
		injectorSettingsINI[settingsSectionShaderDiscovery][settingsNameWorkerThreads] = Globals::gShaderDiscoveryWorkerThreads;
		injectorSettingsINI[settingsSectionShaderDiscovery][settingsNameWorkerThreadPriority] = Globals::gShaderDiscoveryWorkerThreadPriority;
		injectorSettingsINI[settingsSectionShaderDiscovery][settingsNameFrameJobBudget] = Globals::gShaderDiscoveryFrameJobBudget;
		injectorSettingsINI[settingsSectionShaderDiscovery][settingsNamePendingAnalysisLimit] = Globals::gShaderDiscoveryPendingAnalysisLimit;
		injectorSettingsINI[settingsSectionShaderDiscovery][settingsNameQueuedShaderLimit] = Globals::gShaderDiscoveryQueuedShaderLimit;
		injectorSettingsINI[settingsSectionShaderDiscovery][settingsNameMinimumSimilarityScore] = Globals::gShaderDiscoveryMinimumSimilarityScore;
		injectorSettingsINI[settingsSectionShaderDiscovery][settingsNameSimilarityAmbiguityMargin] = Globals::gShaderDiscoverySimilarityAmbiguityMargin;
	}

	bool ReadInjectorSettings()
	{
		const std::string injectorSettingsPath = GetInjectorSettingsPath();

		if (!FileExists(injectorSettingsPath))
		{
			WriteToLogFileWarning("ShaderInjectorIO->ReadInjectorSettings: injector settings ini not found! using default settings... ");
			return false;
		}

		try
		{
			ini::IniFile injectorSettingsINI;
			injectorSettingsINI.load(injectorSettingsPath);

			const int    keyOpenShaderInjectorGUI = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionInjector, settingsNameOpenMenuKey, Globals::keyOpenShaderInjectorGUI);
			const int    keyToggleShaderInjector = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionInjector, settingsNameToggleInjectorKey, Globals::keyToggleShaderInjector);
			const bool   shaderInjectorEnabled = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionInjector, settingsNameInjectorEnabled, Globals::gShaderInjectorEnabled);
			const bool   showShaderInjectorGUI = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionInjector, settingsNameMenuOpen, Globals::gShowShaderInjectorGUI);
			const double shaderInjectorGUIScale = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionInjector, settingsNameMenuScale, static_cast<double>(Globals::gShaderInjectorGUIScale));
			const bool   renderDocIntegrationEnabled = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionRenderDoc, settingsNameEnabled, Globals::gRenderDocIntegrationEnabled);
			const bool   renderDocAutoAttachEnabled = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionRenderDoc, settingsNameAutoAttach, Globals::gRenderDocAutoAttachEnabled);
			const bool   disableLogs = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionLogging, settingsNameDisableLogs, Globals::gDisableLogs);
			const bool   verboseLog = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionLogging, settingsNameVerboseLog, Globals::gVerboseLog);
			const bool   logModifiedShaderNames = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionLogging, settingsNameLogModifiedShaderNames, Globals::gLogModifiedShaderNames);
			const bool   performanceTelemetryEnabled = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionLogging, settingsNamePerformanceTelemetry, Globals::gPerformanceTelemetryEnabled);
			const int    shaderDiscoveryMode = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionShaderDiscovery, settingsNameMode, static_cast<int>(Globals::gShaderDiscoveryMode));
			const int    shaderDiscoveryWorkerThreads = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionShaderDiscovery, settingsNameWorkerThreads, Globals::gShaderDiscoveryWorkerThreads);
			const int    shaderDiscoveryWorkerThreadPriority = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionShaderDiscovery, settingsNameWorkerThreadPriority, Globals::gShaderDiscoveryWorkerThreadPriority);
			const int    shaderDiscoveryFrameJobBudget = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionShaderDiscovery, settingsNameFrameJobBudget, Globals::gShaderDiscoveryFrameJobBudget);
			const int    shaderDiscoveryPendingAnalysisLimit = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionShaderDiscovery, settingsNamePendingAnalysisLimit, Globals::gShaderDiscoveryPendingAnalysisLimit);
			const int    shaderDiscoveryQueuedShaderLimit = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionShaderDiscovery, settingsNameQueuedShaderLimit, Globals::gShaderDiscoveryQueuedShaderLimit);
			const double shaderDiscoveryMinimumSimilarityScore = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionShaderDiscovery, settingsNameMinimumSimilarityScore, Globals::gShaderDiscoveryMinimumSimilarityScore);
			const double shaderDiscoverySimilarityAmbiguityMargin = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionShaderDiscovery, settingsNameSimilarityAmbiguityMargin, Globals::gShaderDiscoverySimilarityAmbiguityMargin);
			const int    vertexShaderModel = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionShaderCompiler, settingsNameVertexShaderModel, static_cast<int>(Globals::gVertexShaderModel));
			const bool   autoDetectShaderModels = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionShaderCompiler, settingsNameAutoDetectShaderModels, Globals::gAutoDetectShaderModels);
			const int    hullShaderModel = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionShaderCompiler, settingsNameHullShaderModel, static_cast<int>(Globals::gHullShaderModel));
			const int    domainShaderModel = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionShaderCompiler, settingsNameDomainShaderModel, static_cast<int>(Globals::gDomainShaderModel));
			const int    geometryShaderModel = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionShaderCompiler, settingsNameGeometryShaderModel, static_cast<int>(Globals::gGeometryShaderModel));
			const int    pixelShaderModel = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionShaderCompiler, settingsNamePixelShaderModel, static_cast<int>(Globals::gPixelShaderModel));
			const int    computeShaderModel = ReadIniValueOrDefault(injectorSettingsINI, settingsSectionShaderCompiler, settingsNameComputeShaderModel, static_cast<int>(Globals::gComputeShaderModel));

			Globals::keyOpenShaderInjectorGUI = keyOpenShaderInjectorGUI;
			Globals::keyToggleShaderInjector = keyToggleShaderInjector;
			Globals::gShaderInjectorEnabled = shaderInjectorEnabled;
			Globals::gShowShaderInjectorGUI = showShaderInjectorGUI;
			Globals::gShaderInjectorGUIScale = static_cast<float>((std::clamp)(shaderInjectorGUIScale, 0.5, 4.0));
			Globals::gRenderDocIntegrationEnabled = renderDocIntegrationEnabled;
			Globals::gRenderDocAutoAttachEnabled = renderDocAutoAttachEnabled;
			Globals::gDisableLogs = disableLogs;
			Globals::gVerboseLog = verboseLog;
			Globals::gLogModifiedShaderNames = logModifiedShaderNames;
			Globals::gPerformanceTelemetryEnabled = performanceTelemetryEnabled;
			Globals::gShaderDiscoveryMode = static_cast<Globals::ShaderDiscoveryMode>((std::clamp)(shaderDiscoveryMode, 0, 1));
			Globals::gShaderDiscoveryWorkerThreads = (std::clamp)(shaderDiscoveryWorkerThreads, 0, 64);
			Globals::gShaderDiscoveryWorkerThreadPriority = (std::clamp)(shaderDiscoveryWorkerThreadPriority, THREAD_PRIORITY_LOWEST, THREAD_PRIORITY_HIGHEST);
			Globals::gShaderDiscoveryFrameJobBudget = (std::clamp)(shaderDiscoveryFrameJobBudget, 1, 65536);
			Globals::gShaderDiscoveryPendingAnalysisLimit = (std::clamp)(shaderDiscoveryPendingAnalysisLimit, 1, 8192);
			Globals::gShaderDiscoveryQueuedShaderLimit = (std::clamp)(shaderDiscoveryQueuedShaderLimit, 1024, 65536);
			Globals::gShaderDiscoveryMinimumSimilarityScore = (std::clamp)(shaderDiscoveryMinimumSimilarityScore, 0.0, 1.0);
			Globals::gShaderDiscoverySimilarityAmbiguityMargin = (std::clamp)(shaderDiscoverySimilarityAmbiguityMargin, 0.0, 1.0);
			Globals::gAutoDetectShaderModels = autoDetectShaderModels;
			Globals::gVertexShaderModel = StringHelper::ShaderModelFromValue(vertexShaderModel, Globals::gVertexShaderModel);
			Globals::gHullShaderModel = StringHelper::ShaderModelFromValue(hullShaderModel, Globals::gHullShaderModel);
			Globals::gDomainShaderModel = StringHelper::ShaderModelFromValue(domainShaderModel, Globals::gDomainShaderModel);
			Globals::gGeometryShaderModel = StringHelper::ShaderModelFromValue(geometryShaderModel, Globals::gGeometryShaderModel);
			Globals::gPixelShaderModel = StringHelper::ShaderModelFromValue(pixelShaderModel, Globals::gPixelShaderModel);
			Globals::gComputeShaderModel = StringHelper::ShaderModelFromValue(computeShaderModel, Globals::gComputeShaderModel);

			WriteToLogFile(
				"ShaderInjectorIO->ReadInjectorSettings: parsed injector settings"
				" renderDocEnabled=" + std::to_string(Globals::gRenderDocIntegrationEnabled) +
				" renderDocAutoAttach=" + std::to_string(Globals::gRenderDocAutoAttachEnabled) +
				" disableLogs=" + std::to_string(Globals::gDisableLogs) +
				" verboseLog=" + std::to_string(Globals::gVerboseLog) +
				" logModifiedShaderNames=" + std::to_string(Globals::gLogModifiedShaderNames) +
				" performanceTelemetry=" + std::to_string(Globals::gPerformanceTelemetryEnabled) +
				" discoveryMode=" + std::to_string(static_cast<int>(Globals::gShaderDiscoveryMode)) +
				" discoveryWorkerThreads=" + std::to_string(Globals::gShaderDiscoveryWorkerThreads) +
				" discoveryWorkerThreadPriority=" + std::to_string(Globals::gShaderDiscoveryWorkerThreadPriority) +
				" discoveryFrameJobBudget=" + std::to_string(Globals::gShaderDiscoveryFrameJobBudget) +
				" discoveryPendingAnalysisLimit=" + std::to_string(Globals::gShaderDiscoveryPendingAnalysisLimit) +
				" discoveryQueuedShaderLimit=" + std::to_string(Globals::gShaderDiscoveryQueuedShaderLimit) +
				" discoveryMinimumSimilarityScore=" + std::to_string(Globals::gShaderDiscoveryMinimumSimilarityScore) +
				" discoverySimilarityAmbiguityMargin=" + std::to_string(Globals::gShaderDiscoverySimilarityAmbiguityMargin) +
				" autoDetectShaderModels=" + std::to_string(Globals::gAutoDetectShaderModels) +
				" vertexShaderProfile=" + StringHelper::ShaderProfileForType(ShaderTarget::VertexShader) +
				" hullShaderProfile=" + StringHelper::ShaderProfileForType(ShaderTarget::HullShader) +
				" domainShaderProfile=" + StringHelper::ShaderProfileForType(ShaderTarget::DomainShader) +
				" geometryShaderProfile=" + StringHelper::ShaderProfileForType(ShaderTarget::GeometryShader) +
				" pixelShaderProfile=" + StringHelper::ShaderProfileForType(ShaderTarget::PixelShader) +
				" computeShaderProfile=" + StringHelper::ShaderProfileForType(ShaderTarget::ComputeShader));
		}
		catch (...)
		{
			WriteToLogFileError("ShaderInjectorIO->ReadInjectorSettings: failed to parse injector settings");
			return false;
		}

		return true;
	}

	void CreateInjectorSettings()
	{
		const std::string injectorSettingsPath = GetInjectorSettingsPath();

		if (FileExists(injectorSettingsPath))
		{
			WriteToLogFileWarning("ShaderInjectorIO->CreateInjectorSettings: injector settings ini already exists!");
			return;
		}

		ini::IniFile injectorSettingsINI;
		PopulateInjectorSettings(injectorSettingsINI);

		std::ostringstream encodedSettings;
		injectorSettingsINI.encode(encodedSettings);

		if (!WriteTextFile(injectorSettingsPath, encodedSettings.str()))
		{
			WriteToLogFileError("ShaderInjectorIO->CreateInjectorSettings: failed to write injector settings: " + injectorSettingsPath);
			return;
		}

		WriteToLogFile("ShaderInjectorIO->CreateInjectorSettings: new injector settings created.");
	}

	bool WriteInjectorSettings()
	{
		const std::string injectorSettingsPath = GetInjectorSettingsPath();

		try
		{
			ini::IniFile injectorSettingsINI;

			if (FileExists(injectorSettingsPath))
				injectorSettingsINI.load(injectorSettingsPath);

			PopulateInjectorSettings(injectorSettingsINI);

			std::ostringstream encodedSettings;
			injectorSettingsINI.encode(encodedSettings);

			if (!WriteTextFile(injectorSettingsPath, encodedSettings.str()))
			{
				WriteToLogFileError("ShaderInjectorIO->WriteInjectorSettings: failed to write injector settings: " + injectorSettingsPath);
				return false;
			}

			WriteToLogFile("ShaderInjectorIO->WriteInjectorSettings: saved injector settings");
			return true;
		}
		catch (...)
		{
			WriteToLogFileError("ShaderInjectorIO->WriteInjectorSettings: failed to update injector settings");
			return false;
		}
	}

	bool WriteInjectorMenuScale(float menuScale)
	{
		const std::string injectorSettingsPath = GetInjectorSettingsPath();

		try
		{
			ini::IniFile injectorSettingsINI;

			if (FileExists(injectorSettingsPath))
				injectorSettingsINI.load(injectorSettingsPath);

			const float clampedMenuScale = (std::clamp)(menuScale, 0.5f, 4.0f);
			injectorSettingsINI[settingsSectionInjector][settingsNameMenuScale] = static_cast<double>(clampedMenuScale);

			std::ostringstream encodedSettings;
			injectorSettingsINI.encode(encodedSettings);

			if (!WriteTextFile(injectorSettingsPath, encodedSettings.str()))
			{
				WriteToLogFileError("ShaderInjectorIO->WriteInjectorMenuScale: failed to write injector settings: " + injectorSettingsPath);
				return false;
			}

			WriteToLogFile("ShaderInjectorIO->WriteInjectorMenuScale: saved menuScale=" + std::to_string(clampedMenuScale));
			return true;
		}
		catch (...)
		{
			WriteToLogFileError("ShaderInjectorIO->WriteInjectorMenuScale: failed to update injector settings");
			return false;
		}
	}
}
