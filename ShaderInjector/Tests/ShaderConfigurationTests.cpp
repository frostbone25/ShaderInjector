#include "IO/ShaderInjectorIO.h"
#include "ShaderConfiguration/DatabaseShaderConfigurations.h"
#include "ShaderConfiguration/ConfigurationProgress.h"
#include "ShaderConfiguration/ConfigurationProgressTask.h"
#include "ShaderConfiguration/ShaderConfigurationCategories.h"
#include "RenderPass/RenderPassConfigurationProperties.h"
#include "ShaderModelDetector.h"
#include "Globals.h"

#include <Windows.h>
#include <atomic>
#include <filesystem>
#include <future>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

bool RunShaderConfigurationExpansionTests(const std::string& testDirectory);
bool RunShaderSourceDependenciesTests(const std::string& testDirectory);
bool RunShaderConfigurationUITests();
void SetConfigurationTestSaveFailure(bool shouldFail);

namespace Globals
{
	bool gShaderInjectorAdvancedMode = false;
}

namespace
{
	std::string testDirectory;
	std::string compilerDirectory;
	int failureCount = 0;
	bool failConfigurationSave = false;
	const std::thread::id frameThreadID = std::this_thread::get_id();
	std::atomic<int> dependencyDirectoryReads = 0;

	void Check(bool condition, const std::string& message)
	{
		if (condition)
			return;

		std::cerr << "FAIL: " << message << '\n';
		++failureCount;
	}

	void WriteFixture(const std::string& relativePath, const std::string& sourceText)
	{
		const std::string sourcePath = ShaderInjectorIO::JoinPath(testDirectory, relativePath);
		ShaderInjectorIO::DirectoryCreate(ShaderInjectorIO::DirectoryFromPath(sourcePath));
		Check(ShaderInjectorIO::WriteTextFile(sourcePath, sourceText), "write fixture " + relativePath);
	}

	void CheckStagedFileCommit()
	{
		WriteFixture("CommitFixture/working.bin", "previous shader");
		WriteFixture("CommitFixture/replacement.stage", "compiled shader");
		const std::string destinationPath = ShaderInjectorIO::JoinPath(testDirectory, "CommitFixture/working.bin");
		const std::string stagedPath = ShaderInjectorIO::JoinPath(testDirectory, "CommitFixture/replacement.stage");
		Check(ShaderInjectorIO::CommitStagedFile(stagedPath, destinationPath), "staged shader atomically replaces its cache file");
		std::string savedText;
		Check(ShaderInjectorIO::ReadTextFile(destinationPath, savedText) && savedText == "compiled shader", "committed cache contains the staged bytes");
		Check(!ShaderInjectorIO::FileExists(stagedPath), "successful commit consumes the staging path");
		Check(!ShaderInjectorIO::CommitStagedFile(stagedPath, destinationPath), "missing staged output is rejected");
		Check(ShaderInjectorIO::ReadTextFile(destinationPath, savedText) && savedText == "compiled shader", "failed commit preserves the existing cache bytes");
	}

	void SetProperty(const std::string& name, const std::string& value)
	{
		bool found = false;

		for (ShaderConfiguration::PropertyDisk& property : DatabaseShaderConfigurations::GetEditableDocument().properties)
		{
			if (property.name != name)
				continue;

			property.value = value;
			found = true;
		}

		Check(found, "find configuration property " + name);
	}

	bool Compile(const std::string& profile, const std::string& outputName, int expectedValue, int expectedPresence, std::shared_ptr<const ShaderConfiguration::CompileConfiguration> configuration = {})
	{
		std::string outputPath = ShaderInjectorIO::JoinPath(testDirectory, outputName + ".blob");
		const std::vector<std::string> variantDefines = {"EXPECTED_VALUE=" + std::to_string(expectedValue), "EXPECTED_PRESENCE=" + std::to_string(expectedPresence)};

		return ShaderInjectorIO::CompileSourceToDXILBlob(ShaderInjectorIO::JoinPath(testDirectory, "Main.hlsl"), profile, "main", outputPath, ShaderInjectorIO::ShaderSignaturePacking::PrefixStable, variantDefines, {}, std::move(configuration));
	}

	void CheckMetadataAndSourcePreparation()
	{
		ShaderConfiguration::DocumentDisk document;
		const std::string sourceText = "// a descriptive comment\n// [CONFIG TYPE]: int\n// [CONFIG DEFAULT]: 4\n// [CONFIG RANGE]: 0, 20\n#define COUNT 2\n// [NO CONFIG]\n#define INTERNAL 9\n#undef COUNT\n#define COUNT 3\n";
		ShaderConfiguration::ParseShaderSource("Metadata.hlsl", sourceText, document.properties);
		Check(document.properties.size() == 2, "metadata parser retains properties and ignores internal defines");
		Check(document.properties[0].description == "a descriptive comment", "comment metadata retained");
		Check(document.properties[0].defaultValue == "4", "default metadata retained");
		Check(!document.properties[0].valueRange.empty(), "range metadata retained");
		document.properties[0].value = "11";
		document.properties[1].value = "22";

		ShaderConfiguration::CompileConfiguration configuration(document);
		std::string preparedSource;
		std::string error;
		Check(configuration.PrepareSource("Metadata.hlsl", sourceText, preparedSource, error), "prepare duplicate macro occurrences");
		Check(configuration.GetDefines().size() == 2 && configuration.GetDefines()[0] != configuration.GetDefines()[1], "duplicate names get independent compiler aliases");
		Check(preparedSource.find("#define INTERNAL 9") != std::string::npos, "ignored define remains intact");
		Check(!configuration.PrepareSource("Metadata.hlsl", "", preparedSource, error), "missing configured define reports an error");
		Check(configuration.PrepareSource("Unrelated.hlsl", sourceText, preparedSource, error) && preparedSource == sourceText, "unrelated source passes through unchanged");
		Check(configuration.PrepareSource("METADATA.HLSL", sourceText, preparedSource, error) && preparedSource.find("SHADER_INJECTOR_CONFIG_") != std::string::npos, "Windows path matching ignores case");

		ShaderConfiguration::CompileConfiguration emptyConfiguration(ShaderConfiguration::DocumentDisk{});
		Check(emptyConfiguration.GetFingerprint().empty(), "empty configuration keeps legacy cache identity");
	}

	void CheckCompilerIntegration()
	{
		//preprocessor assertions prove values reached the compiler, rather than just checking our generated text.
		const std::string mainSource =
			"#include \"nested/First.hlsl\"\n"
			"#include \"Second.hlsl\"\n"
			"// [CONFIG TYPE]: bool\n//#define OPTIONAL_FEATURE\n"
			"#if EXPECTED_PRESENCE\n#ifndef OPTIONAL_FEATURE\n#error enabled presence switch is absent\n#endif\n"
			"#else\n#ifdef OPTIONAL_FEATURE\n#error disabled presence switch is defined\n#endif\n#endif\n"
			"#define CONFIGURED_VALUE 1\n#if CONFIGURED_VALUE != EXPECTED_VALUE\n#error configuration value mismatch\n#endif\n"
			"// [NO CONFIG]\n#define INTERNAL_CONSTANT 77\n#if INTERNAL_CONSTANT != 77\n#error ignored property was changed\n#endif\n"
			"// [CONFIG TYPE]: float3\n#define TINT float3(1.0, 2.0, 3.0)\n"
			"float4 main() : SV_Target { return float4(TINT, 1.0); }\n";
		const std::string firstSource = "#include \"Value.hlsl\"\n#if SHARED_NAME != 11\n#error first include value mismatch\n#endif\n#undef SHARED_NAME\n";
		const std::string nestedSource = "#define SHARED_NAME 1\n";
		const std::string secondSource = "#define SHARED_NAME 2\n#if SHARED_NAME != 22\n#error second include value mismatch\n#endif\n#undef SHARED_NAME\n";

		WriteFixture("Main.hlsl", mainSource);
		WriteFixture("nested/First.hlsl", firstSource);
		WriteFixture("nested/Value.hlsl", nestedSource);
		WriteFixture("Includes/Second.hlsl", secondSource);
		Check(DatabaseShaderConfigurations::ReloadProperties(), "discover source properties");
		SetProperty("CONFIGURED_VALUE", "7");
		SetProperty("OPTIONAL_FEATURE", "true");
		SetProperty("TINT", "[0.25, 0.5, 0.75]");

		//same public name in different source files must not collapse into one compiler setting.
		for (ShaderConfiguration::PropertyDisk& property : DatabaseShaderConfigurations::GetEditableDocument().properties)
		{
			if (property.name == "SHARED_NAME")
			{
				property.value = "11";

				if (property.sourceFile == "Second.hlsl")
					property.value = "22";
			}
		}

		Check(DatabaseShaderConfigurations::ApplyChanges().succeeded, "apply compiler settings");
		const auto appliedConfiguration = DatabaseShaderConfigurations::GetAppliedConfiguration();
		const std::string firstFingerprint = appliedConfiguration->GetFingerprint();
		SetProperty("CONFIGURED_VALUE", "9");
		Check(DatabaseShaderConfigurations::GetAppliedConfiguration()->GetFingerprint() == firstFingerprint, "unsaved edits do not change compiler snapshot");

		for (const std::string& profile : {std::string("ps_5_0"), std::string("ps_6_0")})
			Check(Compile(profile, profile + "-applied", 7, 1), profile + " compiles applied settings and nested includes");

		SetProperty("OPTIONAL_FEATURE", "false");
		Check(DatabaseShaderConfigurations::ApplyChanges().succeeded, "apply second configuration");
		Check(DatabaseShaderConfigurations::GetAppliedConfiguration()->GetFingerprint() != firstFingerprint, "applied values change cache identity");

		//different workers must keep their own compiler interfaces and captured settings.
		std::vector<std::future<bool>> concurrentCompiles;

		for (int workerIndex = 0; workerIndex < 4; ++workerIndex)
		{
			concurrentCompiles.push_back(std::async(std::launch::async, [workerIndex, appliedConfiguration]()
			{
				return Compile("ps_6_0", "worker-" + std::to_string(workerIndex), 7, 1, appliedConfiguration);
			}));
		}

		for (auto& concurrentCompile : concurrentCompiles)
			Check(concurrentCompile.get(), "concurrent DXC compile uses captured configuration");

		for (const std::string& profile : {std::string("ps_5_0"), std::string("ps_6_0")})
		{
			Check(Compile(profile, profile + "-disabled", 9, 0), profile + " disables presence switch");
			Check(Compile(profile, profile + "-snapshot", 7, 1, appliedConfiguration), profile + " retains older captured snapshot");
		}

		SetProperty("CONFIGURED_VALUE", "100");
		Check(DatabaseShaderConfigurations::ReloadProperties(), "refresh property metadata");
		Check(DatabaseShaderConfigurations::GetAppliedConfiguration()->GetFingerprint() != firstFingerprint, "refresh retains latest applied values");
		Check(Compile("ps_6_0", "after-reload", 9, 0), "metadata refresh preserves applied values and discards unsaved edits");

		const auto latestConfiguration = DatabaseShaderConfigurations::GetAppliedConfiguration();
		SetProperty("CONFIGURED_VALUE", "25");
		failConfigurationSave = true;
		Check(!DatabaseShaderConfigurations::ApplyChanges().succeeded, "failed persistence does not apply settings");
		failConfigurationSave = false;
		Check(DatabaseShaderConfigurations::GetAppliedConfiguration() == latestConfiguration, "failed save retains the previous applied snapshot");

		const std::vector<std::pair<std::string, std::string>> originalSources = {{"Main.hlsl", mainSource}, {"nested/First.hlsl", firstSource}, {"nested/Value.hlsl", nestedSource}, {"Includes/Second.hlsl", secondSource}};

		for (const auto& source : originalSources)
		{
			std::string currentText;
			Check(ShaderInjectorIO::ReadTextFile(ShaderInjectorIO::JoinPath(testDirectory, source.first), currentText) && currentText == source.second, "source remains byte-identical: " + source.first);
		}
	}

	void CheckLegacyConfigurationMigration()
	{
		WriteFixture("Legacy.hlsl", "#define LEGACY_SETTING 1\n#undef LEGACY_SETTING\n#define LEGACY_SETTING 2\n");
		nlohmann::ordered_json legacyDocument = {{"schemaVersion", 3}, {"format", "ShaderInjector.ShaderConfigurations"}, {"properties", nlohmann::ordered_json::array()}};

		for (int occurrence = 0; occurrence < 2; ++occurrence)
		{
			int selectedValue = 37;

			if (occurrence == 1)
				selectedValue = 41;

			legacyDocument["properties"].push_back({{"id", "Legacy.hlsl::LEGACY_SETTING::" + std::to_string(occurrence)}, {"name", "LEGACY_SETTING"}, {"sourceFile", "Legacy.hlsl"}, {"sourcePath", "Legacy.hlsl"}, {"type", "int"}, {"comment", "legacy description"}, {"range", "0, 100"}, {"value", std::to_string(selectedValue)}, {"defaultValue", "1"}, {"definitionIndex", occurrence}, {"sourceOrder", occurrence}});
		}

		Check(ShaderInjectorIO::WriteTextFile(ShaderInjectorIO::GetLegacyShaderConfigurationsPath(), legacyDocument.dump(2)), "write legacy migration fixture");
		DatabaseShaderConfigurations::EnsureLoaded();
		const auto appliedDocument = DatabaseShaderConfigurations::GetAppliedDocument();
		Check(appliedDocument.properties.size() == 2, "migration retains repeated source properties");

		if (appliedDocument.properties.size() == 2)
		{
			Check(appliedDocument.properties[0].value == "37" && appliedDocument.properties[1].value == "41", "migration retains selected legacy values");
			Check(appliedDocument.properties[0].description == "legacy description", "migration maps legacy metadata to the property model");
		}

		Check(!DatabaseShaderConfigurations::GetPresets().empty(), "migration creates a default preset");
		Check(ShaderInjectorIO::FileExists(ShaderInjectorIO::GetShaderConfigurationsPath()), "migration saves catalog in ShaderConfiguration folder");
		Check(ShaderInjectorIO::FileExists(ShaderInjectorIO::GetLegacyShaderConfigurationsPath()), "migration retains the legacy file");
	}

	DatabaseShaderConfigurations::ConfigurationOperationResult WaitForConfigurationOperation()
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

		while (std::chrono::steady_clock::now() < deadline)
		{
			const auto result = DatabaseShaderConfigurations::ProcessCompletedOperation();

			if (result.completed)
				return result;

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		Check(false, "configuration worker completes within its test deadline");
		return {};
	}

	void CheckConfigurationOperationProgress()
	{
		WriteFixture("StartupUnrelated.hlsl", "float4 main() : SV_Target { return 0; }\n");
		auto startupPreparation = std::async(std::launch::async, [] { DatabaseShaderConfigurations::PrepareStartupConfiguration(); });
		startupPreparation.get();
		Check(DatabaseShaderConfigurations::GetAppliedConfiguration()->GetSourceFingerprint("StartupUnrelated.hlsl").empty(), "startup worker prepares source-scoped cache identities before package loading");

		const auto previousConfiguration = DatabaseShaderConfigurations::GetAppliedConfiguration();
		const int previousDependencyReads = dependencyDirectoryReads.load();
		SetProperty("CONFIGURED_VALUE", "31");
		Check(DatabaseShaderConfigurations::QueueApplyChanges(), "start asynchronous apply");
		Check(!DatabaseShaderConfigurations::QueueReloadProperties(), "overlapping reload is rejected");
		Check(DatabaseShaderConfigurations::GetAppliedConfiguration() == previousConfiguration, "worker does not publish settings before frame drain");
		const auto applyResult = WaitForConfigurationOperation();
		Check(applyResult.completed && applyResult.applying && applyResult.succeeded, "asynchronous apply prepares values successfully");
		Check(dependencyDirectoryReads.load() > previousDependencyReads, "dependency discovery finishes on the worker before configuration publication");
		const auto progress = DatabaseShaderConfigurations::GetOperationProgressTracker();
		Check(DatabaseShaderConfigurations::GetAppliedConfiguration()->GetProgress() == progress, "applied snapshot carries the operation tracker");
		const auto preparationSnapshot = progress->GetSnapshot();
		Check(!preparationSnapshot.compilationBegun && !preparationSnapshot.dispatchFinished && preparationSnapshot.blocksEditing, "configuration blocks edits until compilation dispatch begins");

		//a parent compile stays pending until its dependent variants have joined the same operation.
		progress->BeginCompiling();
		auto parentTask = progress->BeginTask(true);
		progress->FinishDispatch();
		Check(progress->GetSnapshot().blocksEditing, "in-flight priority work blocks edits");
		Check(!DatabaseShaderConfigurations::QueueReloadProperties(), "new operation waits for shader loading");
		auto childTask = progress->BeginTask();
		parentTask->MarkSucceeded();
		parentTask.reset();
		Check(!progress->GetSnapshot().finished && progress->GetSnapshot().completed == 1, "published parent does not finish while a variant is pending");
		childTask->MarkFailed();
		childTask.reset();
		const auto completedProgress = progress->GetSnapshot();
		Check(completedProgress.finished && completedProgress.total == 2 && completedProgress.completed == 2 && completedProgress.failed == 1, "operation finishes with accurate shader failure counts");
		Check(completedProgress.priorityFailed == 0 && !completedProgress.blocksEditing, "completed priority success remains terminal after background failure");
		Check(!progress->BeginTask(), "later unrelated work does not reopen a finished operation");

		auto priorityReadyProgress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		priorityReadyProgress->BeginCompiling();
		auto activeTask = priorityReadyProgress->BeginTask(true);
		auto inactiveTask = priorityReadyProgress->BeginTask();
		activeTask->MarkSucceeded();
		activeTask.reset();
		Check(priorityReadyProgress->GetSnapshot().blocksEditing, "priority completion before dispatch seal does not unlock edits");
		priorityReadyProgress->FinishDispatch();
		const auto priorityReadySnapshot = priorityReadyProgress->GetSnapshot();
		Check(!priorityReadySnapshot.blocksEditing && !priorityReadySnapshot.finished && priorityReadySnapshot.priorityCompleted == 1, "successful priority work unlocks edits while inactive work continues");
		inactiveTask.reset();

		auto zeroPriorityProgress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		zeroPriorityProgress->BeginCompiling();
		auto zeroPriorityBackgroundTask = zeroPriorityProgress->BeginTask();
		zeroPriorityProgress->FinishDispatch();
		const auto zeroPrioritySnapshot = zeroPriorityProgress->GetSnapshot();
		Check(zeroPrioritySnapshot.priorityTotal == 0 && !zeroPrioritySnapshot.blocksEditing && !zeroPrioritySnapshot.finished, "zero-priority batches unlock only after dispatch without implying readiness");
		zeroPriorityBackgroundTask.reset();

		auto failedPriorityProgress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		failedPriorityProgress->BeginCompiling();
		auto failedPriorityTask = failedPriorityProgress->BeginTask(true);
		auto failedPriorityBackgroundTask = failedPriorityProgress->BeginTask();
		failedPriorityProgress->FinishDispatch();
		failedPriorityTask->MarkFailed();
		failedPriorityTask.reset();
		const auto failedPrioritySnapshot = failedPriorityProgress->GetSnapshot();
		Check(!failedPrioritySnapshot.blocksEditing && failedPrioritySnapshot.priorityFailed == 1, "failed priority work unlocks retry without claiming success");
		failedPriorityBackgroundTask.reset();

		auto promotedPriorityProgress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		promotedPriorityProgress->BeginCompiling();
		auto promotedTask = promotedPriorityProgress->BeginTask();
		promotedPriorityProgress->FinishDispatch();
		Check(!promotedPriorityProgress->GetSnapshot().blocksEditing, "zero-priority work is editable after dispatch");
		promotedTask->PromotePriority();
		Check(promotedPriorityProgress->GetSnapshot().blocksEditing, "late priority promotion closes the edit gate again");
		promotedTask->MarkSucceeded();
		promotedTask.reset();
		Check(!promotedPriorityProgress->GetSnapshot().blocksEditing, "promoted priority task reopens edits after successful completion");

		SetProperty("CONFIGURED_VALUE", "999");
		Check(DatabaseShaderConfigurations::QueueReloadProperties(), "start asynchronous metadata reload");
		const auto reloadResult = WaitForConfigurationOperation();
		Check(reloadResult.completed && !reloadResult.applying && reloadResult.succeeded, "metadata reload completes successfully");
		const auto reloadProgress = DatabaseShaderConfigurations::GetOperationProgressTracker();
		reloadProgress->FinishDispatch();
		Check(reloadProgress->GetSnapshot().finished && reloadProgress->GetSnapshot().completed == reloadProgress->GetSnapshot().total, "reload counts parsing and publication through completion");
		Check(Compile("ps_6_0", "after-async-reload", 31, 0), "async reload retains applied settings");

		const int dependencyReadsBeforeForce = dependencyDirectoryReads.load();
		SetProperty("CONFIGURED_VALUE", "32");
		Check(DatabaseShaderConfigurations::QueueApplyChanges(true), "force apply prepares changed values asynchronously");
		const auto forcedResult = WaitForConfigurationOperation();
		Check(forcedResult.succeeded && forcedResult.forceRecompileAll, "force mode survives asynchronous preparation");
		Check(dependencyDirectoryReads.load() > dependencyReadsBeforeForce, "changed force settings prepare source-scoped cache identities on the worker");
		const auto supersededProgress = DatabaseShaderConfigurations::GetOperationProgressTracker();
		supersededProgress->BeginCompiling();
		auto activeForceTask = supersededProgress->BeginTask(true);
		auto inactiveForceTask = supersededProgress->BeginTask();
		supersededProgress->FinishDispatch();
		activeForceTask->MarkSucceeded();
		activeForceTask.reset();
		Check(!supersededProgress->GetSnapshot().blocksEditing && !supersededProgress->GetSnapshot().finished, "forced apply unlocks edits while inactive variants are pending");
		Check(Compile("ps_6_0", "after-async-force", 32, 0), "force apply supplies its changed settings to the compiler");

		const int dependencyReadsAfterForce = dependencyDirectoryReads.load();
		Check(DatabaseShaderConfigurations::QueueApplyChanges(), "unchanged apply reuses prepared identities");
		const auto supersedingApplyResult = WaitForConfigurationOperation();
		Check(supersedingApplyResult.succeeded, "unchanged apply completes after priority-ready work");
		const auto nextProgress = DatabaseShaderConfigurations::GetOperationProgressTracker();
		Check(nextProgress->GetOperationID() != supersededProgress->GetOperationID(), "new apply starts a distinct operation generation");
		Check(supersededProgress->GetSnapshot().cancellationRequested && supersededProgress->GetSnapshot().canceled == 1, "new apply cancels the previous inactive batch");
		const size_t canceledCount = supersededProgress->GetSnapshot().canceled;
		inactiveForceTask.reset();
		Check(supersededProgress->GetSnapshot().canceled == canceledCount, "late old-task completion cannot mutate a superseded tracker");
		Check(dependencyDirectoryReads.load() == dependencyReadsAfterForce, "unchanged shader properties skip dependency discovery");
		nextProgress->FinishDispatch();

		const auto savedConfiguration = DatabaseShaderConfigurations::GetAppliedConfiguration();
		failConfigurationSave = true;
		Check(DatabaseShaderConfigurations::QueueApplyChanges(), "start failing asynchronous save");
		const auto failureResult = WaitForConfigurationOperation();
		failConfigurationSave = false;
		Check(failureResult.completed && !failureResult.succeeded, "asynchronous save reports failure");
		Check(DatabaseShaderConfigurations::GetAppliedConfiguration() == savedConfiguration, "failed asynchronous save retains working configuration");
		Check(DatabaseShaderConfigurations::GetOperationProgress().finished && !DatabaseShaderConfigurations::GetOperationProgress().errorMessage.empty(), "failure finishes progress with an explanation");

		const auto cancellationProgress = std::make_shared<ShaderConfiguration::ConfigurationProgress>();
		cancellationProgress->BeginCompiling();
		auto canceledTask = cancellationProgress->BeginTask();
		cancellationProgress->FinishDispatch();
		canceledTask.reset();
		Check(cancellationProgress->GetSnapshot().finished && cancellationProgress->GetSnapshot().canceled == 1, "superseded compile finishes its progress task");
	}
}

void SetConfigurationTestSaveFailure(bool shouldFail)
{
	failConfigurationSave = shouldFail;
}

//only application paths and logging are replaced; parsing, persistence, file IO, and both compilers are production code.
namespace ShaderInjectorIO
{
	ProcessResult RunProcess(const std::string&, const std::vector<std::string>&, const std::string&)
	{
		throw std::logic_error("shader configuration tests compile through the compiler DLLs");
	}

	std::string GetModifiedShadersDirectory() { return testDirectory; }
	std::string GetRenderPassesDirectory()
	{
		//fail if dependency preparation ever moves back onto the simulated frame thread.
		if (std::this_thread::get_id() == frameThreadID)
			throw std::logic_error("dependency source discovery ran on the frame thread");

		++dependencyDirectoryReads;
		return JoinPath(testDirectory, "RenderPasses");
	}
	std::string GetModifiedShadersIncludesDirectory() { return JoinPath(testDirectory, "Includes"); }
	std::string GetShaderConfigurationDirectory() { return JoinPath(testDirectory, "ShaderConfiguration"); }
	std::string GetShaderConfigurationPresetsDirectory() { return JoinPath(GetShaderConfigurationDirectory(), "Presets"); }
	std::string GetLegacyShaderConfigurationsPath() { return JoinPath(testDirectory, "ShaderConfigurations.json"); }
	std::string GetShaderConfigurationsPath()
	{
		if (failConfigurationSave)
			return testDirectory;

		return JoinPath(GetShaderConfigurationDirectory(), "Properties.json");
	}
	std::string GetToolPathDXCompiler() { return JoinPath(compilerDirectory, "dxcompiler.dll"); }
	std::string GetToolPathDXC() { return JoinPath(compilerDirectory, "dxc.exe"); }
	void WriteToLogFile(const std::string& message) { std::cout << message << '\n'; }
	void WriteToLogFileError(const std::string& message) { std::cerr << message << '\n'; }
	void WriteToLogFileWarning(const std::string& message) { std::cerr << message << '\n'; }
	void WriteToLogFileSuccess(const std::string& message) { std::cout << message << '\n'; }
}

namespace ShaderInjectorGUI
{
	void WriteToRuntimeLogError(std::string message) { std::cerr << message << '\n'; }
	void WriteToRuntimeLogWarning(std::string message) { std::cerr << message << '\n'; }
}

namespace ShaderModelDetector
{
	Globals::ShaderModel GetEffectiveShaderModel(ShaderTarget::ShaderType, Globals::ShaderModel configuredShaderModel) { return configuredShaderModel; }
}

namespace StringHelper
{
	std::string ShaderModelToString(Globals::ShaderModel) { return "6_0"; }
}

int main()
{
	const auto workspaceDirectory = std::filesystem::current_path();
	compilerDirectory = ShaderInjectorIO::PathToUTF8(workspaceDirectory / "ExternalDependencies/DirectXCompiler");
	testDirectory = ShaderInjectorIO::PathToUTF8(workspaceDirectory / "x64/Validation/ShaderConfiguration" / std::to_string(GetTickCount64()));
	ShaderInjectorIO::DirectoryCreate(testDirectory);
	CheckStagedFileCommit();
	CheckMetadataAndSourcePreparation();
	CheckLegacyConfigurationMigration();
	CheckCompilerIntegration();
	CheckConfigurationOperationProgress();
	Check(RunShaderConfigurationExpansionTests(testDirectory), "expanded presets, categories, and render-pass properties");
	Check(RunShaderSourceDependenciesTests(testDirectory), "transitive shader source dependency fingerprints");
	Check(RunShaderConfigurationUITests(), "configuration UI stacks, selection, and layout");

	if (failureCount != 0)
		return 1;

	std::cout << "All shader configuration checks passed (FXC and DXC).\n";
	return 0;
}
