#include "PreCompiledHeader.h"

#include "DatabaseShaderReplacements.h"
#include "DatabaseShaderSources.h"
#include "HookD3D12ReplacementTemplates.h"
#include "ShaderInjectorGUI.h"
#include "ShaderInjectorIO.h"
#include "ShaderReplacement.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

namespace HookD3D12
{
	std::vector<ShaderReplacement::ShaderReplacementDisk> gLoadedShaderReplacements;
	std::vector<std::vector<uint8_t>> gLoadedShaderReplacementBlobs;
	int gSelectedShaderReplacementIndex = -1;
	int gShaderReplacementNameBufferIndex = -1;
	char gShaderReplacementNameBuffer[256]{};
	bool gLoadedShaderReplacementsOnce = false;

	void RefreshLoadedShaderReplacements()
	{
		gLoadedShaderReplacements.clear();
		gLoadedShaderReplacementBlobs.clear();
		gSelectedShaderReplacementIndex = -1;
		gShaderReplacementNameBufferIndex = -1;
		gShaderReplacementNameBuffer[0] = '\0';

		const std::string replacementDirectory = ShaderInjectorIO::GetShaderReplacementsDirectory();

		if (!ShaderInjectorIO::DirectoryExists(replacementDirectory))
			ShaderInjectorIO::DirectoryCreate(replacementDirectory);

		std::vector<std::string> jsonFiles;
		ShaderReplacement::CollectShaderReplacementJsonFiles(replacementDirectory, jsonFiles);
		std::sort(jsonFiles.begin(), jsonFiles.end());

		for (const std::string& jsonPath : jsonFiles)
		{
			ShaderReplacement::ShaderReplacementDisk replacement{};

			if (ShaderReplacement::LoadShaderReplacementJson(jsonPath, replacement))
			{
				BackfillReplacementPortableMetadataFromSidecars(replacement);
				std::vector<uint8_t> modifiedBlob;

				if (!replacement.modifiedShaderBlobPath.empty() && ShaderInjectorIO::FileExists(replacement.modifiedShaderBlobPath))
					ShaderInjectorIO::LoadDXILBlobFromDisk(replacement.modifiedShaderBlobPath, modifiedBlob);

				gLoadedShaderReplacements.push_back(replacement);
				gLoadedShaderReplacementBlobs.push_back(modifiedBlob);
			}
		}

		if (!gLoadedShaderReplacements.empty())
			gSelectedShaderReplacementIndex = 0;

		ResetUncapturedReplacementAttempts();
		gLoadedShaderReplacementsOnce = true;
		MarkShaderReplacementApplyDirty();
		ShaderInjectorGUI::WriteToRuntimeLog("DatabaseShaderReplacements->RefreshLoadedShaderReplacements: Loaded shader replacements from disk: " + std::to_string(gLoadedShaderReplacements.size()));

		for (const auto& replacement : gLoadedShaderReplacements)
		{
			ShaderInjectorGUI::WriteToRuntimeLog(
				"Replacement loaded: " + replacement.name +
				" enabled=" + std::to_string(replacement.enabled ? 1 : 0) +
				" shaderHash=" + replacement.originalShaderBytecodeHash +
				" cacheHash=" + replacement.pipelineCachedBlobHash +
				" cacheBytes=" + replacement.pipelineCachedBlobLength +
				" source=" + replacement.sourceList +
				" index=" + replacement.pipelineIndex);
		}
	}

	void SyncShaderReplacementNameBuffer()
	{
		if (gSelectedShaderReplacementIndex < 0 || gSelectedShaderReplacementIndex >= (int)gLoadedShaderReplacements.size())
		{
			gShaderReplacementNameBufferIndex = -1;
			gShaderReplacementNameBuffer[0] = '\0';
			return;
		}

		if (gShaderReplacementNameBufferIndex == gSelectedShaderReplacementIndex)
			return;

		const std::string& name = gLoadedShaderReplacements[gSelectedShaderReplacementIndex].name;
		strncpy_s(gShaderReplacementNameBuffer, name.c_str(), _TRUNCATE);
		gShaderReplacementNameBufferIndex = gSelectedShaderReplacementIndex;
	}

	bool SaveShaderReplacement(int index)
	{
		if (index < 0 || index >= (int)gLoadedShaderReplacements.size())
			return false;

		ShaderReplacement::ShaderReplacementDisk& replacement = gLoadedShaderReplacements[index];
		replacement.name = gShaderReplacementNameBuffer;

		if (!ShaderReplacement::WriteShaderReplacementJson(replacement))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseShaderReplacements->SaveShaderReplacement: failed to save " + replacement.jsonPath);
			return false;
		}

		MarkShaderReplacementApplyDirty();
		ShaderInjectorGUI::WriteToRuntimeLog("DatabaseShaderReplacements->SaveShaderReplacement: Saved shader replacement: " + replacement.jsonPath);
		return true;
	}

	bool CompileShaderReplacement(int index)
	{
		if (index < 0 || index >= (int)gLoadedShaderReplacements.size())
			return false;

		ShaderReplacement::ShaderReplacementDisk& replacement = gLoadedShaderReplacements[index];

		if (!replacement.shaderSourceName.empty())
			replacement.shaderSourcePath = DatabaseShaderSources::ResolveShaderSourcePath(replacement.shaderType, replacement.shaderSourceName);

		if (replacement.shaderSourcePath.empty() || !ShaderInjectorIO::FileExists(replacement.shaderSourcePath))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseShaderReplacements->CompileShaderReplacement: source file not found " + replacement.shaderSourcePath);
			return false;
		}

		if (replacement.shaderProfile.empty() || replacement.shaderEntryPoint.empty())
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseShaderReplacements->CompileShaderReplacement: shader profile or entry point missing for " + replacement.name);
			return false;
		}

		std::string compiledBlobPath = replacement.modifiedShaderBlobPath;

		if (!ShaderInjectorIO::CompileSourceToDXILBlob(replacement.shaderSourcePath, replacement.shaderProfile, replacement.shaderEntryPoint, compiledBlobPath))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseShaderReplacements->CompileShaderReplacement: compile failed for " + replacement.shaderSourcePath);
			return false;
		}

		replacement.modifiedShaderBlobPath = compiledBlobPath;
		ShaderReplacement::WriteShaderReplacementJson(replacement);
		ShaderInjectorGUI::WriteToRuntimeLogSuccess("DatabaseShaderReplacements->CompileShaderReplacement: Compiled shader replacement: " + replacement.name + " -> " + replacement.modifiedShaderBlobPath);
		return true;
	}

	bool ReloadShaderReplacement(int index)
	{
		if (index < 0 || index >= (int)gLoadedShaderReplacements.size())
			return false;

		ShaderReplacement::ShaderReplacementDisk& replacement = gLoadedShaderReplacements[index];

		if (index >= (int)gLoadedShaderReplacementBlobs.size())
			gLoadedShaderReplacementBlobs.resize(gLoadedShaderReplacements.size());

		std::vector<uint8_t> loadedBlob;

		if (replacement.modifiedShaderBlobPath.empty() || !ShaderInjectorIO::LoadDXILBlobFromDisk(replacement.modifiedShaderBlobPath, loadedBlob) || loadedBlob.empty())
		{
			gLoadedShaderReplacementBlobs[index].clear();
			ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseShaderReplacements->ReloadShaderReplacement: failed to load compiled blob " + replacement.modifiedShaderBlobPath);
		}
		else
		{
			gLoadedShaderReplacementBlobs[index] = loadedBlob;
			ShaderInjectorGUI::WriteToRuntimeLog("DatabaseShaderReplacements->ReloadShaderReplacement: Reloaded shader replacement: " + replacement.name + " bytes=" + std::to_string(gLoadedShaderReplacementBlobs[index].size()));
		}

		{
			std::lock_guard<std::mutex> lock(gPipelineMutex);
			InvalidateAllReplacementPSOs();
		}

		MarkShaderReplacementApplyDirty();
		return !gLoadedShaderReplacementBlobs[index].empty();
	}

	bool DeleteShaderReplacement(int index)
	{
		if (index < 0 || index >= (int)gLoadedShaderReplacements.size())
			return false;

		ShaderReplacement::ShaderReplacementDisk replacement = gLoadedShaderReplacements[index];

		ShaderInjectorIO::DeleteFileIfExists(replacement.modifiedShaderBlobPath);
		ShaderInjectorIO::DeleteFileIfExists(replacement.originalShaderBlobPath);
		ShaderInjectorIO::DeleteFileIfExists(replacement.jsonPath);

		if (!replacement.replacementDirectory.empty() && ShaderInjectorIO::DirectoryExists(replacement.replacementDirectory))
			RemoveDirectoryA(replacement.replacementDirectory.c_str());

		{
			std::lock_guard<std::mutex> lock(gPipelineMutex);
			InvalidateAllReplacementPSOs();
		}

		ShaderInjectorGUI::WriteToRuntimeLog("DatabaseShaderReplacements->DeleteShaderReplacement: Deleted shader replacement: " + replacement.name);
		RefreshLoadedShaderReplacements();
		MarkShaderReplacementApplyDirty();
		return true;
	}
}
