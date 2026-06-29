//DatabaseShaderReplacements.cpp
#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

//custom
#include "DatabaseShaderReplacements.h"
#include "DatabaseShaderSources.h"
#include "HookD3D12ReplacementTemplates.h"
#include "HookD3D12ReplacementLookup.h"
#include "ShaderInjectorGUI.h"
#include "ShaderInjectorIO.h"
#include "ShaderReplacement.h"

namespace HookD3D12
{
	//runtime mirror of the ShaderReplacements directory
	//JSON files are still the source of truth on disk.
	std::vector<ShaderReplacement::ShaderReplacementDisk> gLoadedShaderReplacements;

	std::vector<std::vector<uint8_t>> gLoadedShaderReplacementBlobs;
	int gSelectedShaderReplacementIndex = -1;
	int gShaderReplacementNameBufferIndex = -1;
	char gShaderReplacementNameBuffer[256]{};
	bool gLoadedShaderReplacementsOnce = false;

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| COMPILE SHADER REPLACEMENT |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| COMPILE SHADER REPLACEMENT |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| COMPILE SHADER REPLACEMENT |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//using the selected index of currently selected shader replacement...
	//find the source HLSL shader file it is pointing to and compile it into a ready to use shader bytecode blob
	bool CompileShaderReplacement(int replacementIndex)
	{
		if (replacementIndex < 0 || replacementIndex >= (int)gLoadedShaderReplacements.size())
			return false;

		ShaderReplacement::ShaderReplacementDisk& replacement = gLoadedShaderReplacements[replacementIndex];

		//JSON stores only the source filename, this is intentional as we don't want absolute paths, only relative
		//so resolve the full path using the ShaderSources directory path
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

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| RELOAD SHADER REPLACEMENTS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| RELOAD SHADER REPLACEMENTS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| RELOAD SHADER REPLACEMENTS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//using the selected index of currently selected shader replacement...
	//"reload" the shader replacement by loading compiled shader blobs from disk
	bool ReloadShaderReplacement(int replacementIndex)
	{
		if (replacementIndex < 0 || replacementIndex >= (int)gLoadedShaderReplacements.size())
			return false;

		ShaderReplacement::ShaderReplacementDisk& replacement = gLoadedShaderReplacements[replacementIndex];

		if (replacementIndex >= (int)gLoadedShaderReplacementBlobs.size())
			gLoadedShaderReplacementBlobs.resize(gLoadedShaderReplacements.size());

		std::vector<uint8_t> compiledReplacementBlob;

		if (replacement.modifiedShaderBlobPath.empty() || !ShaderInjectorIO::LoadDXILBlobFromDisk(replacement.modifiedShaderBlobPath, compiledReplacementBlob) || compiledReplacementBlob.empty())
		{
			gLoadedShaderReplacementBlobs[replacementIndex].clear();
			ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseShaderReplacements->ReloadShaderReplacement: failed to load compiled blob " + replacement.modifiedShaderBlobPath);
		}
		else
		{
			gLoadedShaderReplacementBlobs[replacementIndex] = compiledReplacementBlob;
			ShaderInjectorGUI::WriteToRuntimeLog("DatabaseShaderReplacements->ReloadShaderReplacement: Reloaded shader replacement: " + replacement.name + " bytes=" + std::to_string(gLoadedShaderReplacementBlobs[replacementIndex].size()));
		}

		//IMPORTANT NOTE: brackets are important here, we need to limit the scope when collecting the root signature
		{
			//a newly loaded blob invalidates all rebuilt replacement PSOs; they must be recreated with the new bytecode.
			std::lock_guard<std::mutex> lock(gPipelineMutex);
			InvalidateAllReplacementPSOs();
		}

		//IMPORTANT: let the rest of the injector know that our shader replacement is dirty (needs to be updated)
		MarkShaderReplacementApplyDirty();
		return !gLoadedShaderReplacementBlobs[replacementIndex].empty();
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SAVE SHADER REPLACEMENT |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SAVE SHADER REPLACEMENT |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SAVE SHADER REPLACEMENT |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//using the selected index of currently selected shader replacement...
	//save any changes made to the shader replacement to the disk
	bool SaveShaderReplacement(int replacementIndex)
	{
		if (replacementIndex < 0 || replacementIndex >= (int)gLoadedShaderReplacements.size())
			return false;

		ShaderReplacement::ShaderReplacementDisk& replacement = gLoadedShaderReplacements[replacementIndex];
		replacement.name = gShaderReplacementNameBuffer;

		if (!ShaderReplacement::WriteShaderReplacementJson(replacement))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseShaderReplacements->SaveShaderReplacement: failed to save " + replacement.jsonPath);
			return false;
		}

		//IMPORTANT: let the rest of the injector know that our shader replacement is dirty (needs to be updated)
		MarkShaderReplacementApplyDirty();

		ShaderInjectorGUI::WriteToRuntimeLog("DatabaseShaderReplacements->SaveShaderReplacement: Saved shader replacement: " + replacement.jsonPath);
		return true;
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| DELETE SHADER REPLACEMENT |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| DELETE SHADER REPLACEMENT |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| DELETE SHADER REPLACEMENT |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//using the selected index of currently selected shader replacement...
	//delete a shader replacement from the disk (and memory)!
	bool DeleteShaderReplacement(int replacementIndex)
	{
		if (replacementIndex < 0 || replacementIndex >= (int)gLoadedShaderReplacements.size())
			return false;

		ShaderReplacement::ShaderReplacementDisk replacement = gLoadedShaderReplacements[replacementIndex];

		ShaderInjectorIO::DeleteFileIfExists(replacement.modifiedShaderBlobPath);
		ShaderInjectorIO::DeleteFileIfExists(replacement.originalShaderBlobPath);
		ShaderInjectorIO::DeleteFileIfExists(replacement.jsonPath);

		if (!replacement.replacementDirectory.empty() && ShaderInjectorIO::DirectoryExists(replacement.replacementDirectory))
			RemoveDirectoryA(replacement.replacementDirectory.c_str());

		//IMPORTANT NOTE: brackets are important here, we need to limit the scope when collecting the root signature
		{
			std::lock_guard<std::mutex> lock(gPipelineMutex);
			InvalidateAllReplacementPSOs();
		}

		ShaderInjectorGUI::WriteToRuntimeLog("DatabaseShaderReplacements->DeleteShaderReplacement: Deleted shader replacement: " + replacement.name);
		
		//we need to refresh the list so we are up to date with the shader replacements that exist in the folder
		RefreshLoadedShaderReplacements();
		
		//IMPORTANT: let the rest of the injector know that our shader replacement is dirty (needs to be updated)
		MarkShaderReplacementApplyDirty();

		return true;
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| REFRESH SHADER REPLACEMENTS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| REFRESH SHADER REPLACEMENTS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| REFRESH SHADER REPLACEMENTS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//recollect shader replacements from the shader replacement folder, and load them into memory
	void RefreshLoadedShaderReplacements()
	{
		//rebuild the runtime list from disk so external edits or deleted replacement folders are reflected in the UI.
		ResetCachedBlobContentLookup();
		gLoadedShaderReplacements.clear();
		gLoadedShaderReplacementBlobs.clear();
		gSelectedShaderReplacementIndex = -1;
		gShaderReplacementNameBufferIndex = -1;
		gShaderReplacementNameBuffer[0] = '\0';

		const std::string replacementDirectory = ShaderInjectorIO::GetShaderReplacementsDirectory();

		if (!ShaderInjectorIO::DirectoryExists(replacementDirectory))
			ShaderInjectorIO::DirectoryCreate(replacementDirectory);

		std::vector<std::string> replacementJsonPaths;
		ShaderReplacement::CollectShaderReplacementJsonFiles(replacementDirectory, replacementJsonPaths);
		std::sort(replacementJsonPaths.begin(), replacementJsonPaths.end());

		for (const std::string& replacementJsonPath : replacementJsonPaths)
		{
			ShaderReplacement::ShaderReplacementDisk replacement{};

			if (ShaderReplacement::LoadShaderReplacementJson(replacementJsonPath, replacement))
			{
				BackfillReplacementPortableMetadataFromSidecars(replacement);
				std::vector<uint8_t> compiledReplacementBlob;

				if (!replacement.modifiedShaderBlobPath.empty() && ShaderInjectorIO::FileExists(replacement.modifiedShaderBlobPath))
					ShaderInjectorIO::LoadDXILBlobFromDisk(replacement.modifiedShaderBlobPath, compiledReplacementBlob);

				gLoadedShaderReplacements.push_back(replacement);
				gLoadedShaderReplacementBlobs.push_back(compiledReplacementBlob);
			}
		}

		if (!gLoadedShaderReplacements.empty())
			gSelectedShaderReplacementIndex = 0;

		ResetUncapturedReplacementAttempts();
		gLoadedShaderReplacementsOnce = true;

		//IMPORTANT: let the rest of the injector know that our shader replacement is dirty (needs to be updated)
		MarkShaderReplacementApplyDirty();

		ShaderInjectorGUI::WriteToRuntimeLog("DatabaseShaderReplacements->RefreshLoadedShaderReplacements: Loaded shader replacements from disk: " + std::to_string(gLoadedShaderReplacements.size()));

		for (const auto& replacement : gLoadedShaderReplacements)
		{
			ShaderInjectorGUI::WriteToRuntimeLog(
				"DatabaseShaderReplacements->RefreshLoadedShaderReplacements: Replacement loaded: " + replacement.name +
				" enabled=" + std::to_string(replacement.enabled ? 1 : 0) +
				" shaderHash=" + replacement.originalShaderBytecodeHash +
				" cacheHash=" + replacement.pipelineCachedBlobHash +
				" cacheBytes=" + replacement.pipelineCachedBlobLength +
				" source=" + replacement.sourceList +
				" index=" + replacement.pipelineIndex);
		}
	}

	//utility function for UI
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
}
