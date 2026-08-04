//DatabaseShaderTargets.cpp
#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

//custom
#include "DatabaseShaderTargets.h"
#include "DatabaseModifiedShaders.h"
#include "Globals.h"
#include "HookD3D12ReplacementTemplates.h"
#include "HookD3D12ReplacementLookup.h"
#include "ShaderDiscovery.h"
#include "ShaderInjectorGUI.h"
#include "ShaderInjectorIO.h"
#include "ShaderTarget.h"

namespace HookD3D12
{
	//runtime mirror of the ShaderTargets directory
	//JSON files are still the source of truth on disk.
	std::vector<ShaderTarget::ShaderTargetDisk> gLoadedShaderTargets;

	std::vector<std::vector<uint8_t>> gLoadedShaderTargetBlobs;
	int gSelectedShaderTargetIndex = -1;
	int gShaderTargetNameBufferIndex = -1;
	char gShaderTargetNameBuffer[256]{};
	bool gLoadedShaderTargetsOnce = false;

	bool IsShaderTargetEffectivelyEnabled(const ShaderTarget::ShaderTargetDisk& replacement)
	{
		if (!replacement.enabled || replacement.modifiedShaderId.empty())
			return false;

		const ModifiedShader::PackageDisk* modifiedShader =
			DatabaseModifiedShaders::FindModifiedShaderById(replacement.modifiedShaderId);
		return modifiedShader && modifiedShader->enabled &&
			modifiedShader->shaderType == replacement.shaderType;
	}

	void RefreshShaderTargetsForModifiedShaderStateChange()
	{
		// Existing replacement PSOs were built from the previous effective state.
		// Retire them before reloading linked blobs so the next apply pass either
		// rebuilds from the enabled package or falls back to the original game PSO.
		{
			std::lock_guard<std::mutex> lock(gPipelineMutex);
			InvalidateAllReplacementPSOs();
		}

		RefreshLoadedShaderTargets();
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| COMPILE SHADER REPLACEMENT |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| COMPILE SHADER REPLACEMENT |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| COMPILE SHADER REPLACEMENT |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//using the selected index of currently selected shader replacement...
	//find the source HLSL shader file it is pointing to and compile it into a ready to use shader bytecode blob
	bool CompileShaderTarget(int replacementIndex)
	{
		if (replacementIndex < 0 || replacementIndex >= (int)gLoadedShaderTargets.size())
			return false;

		ShaderTarget::ShaderTargetDisk& replacement = gLoadedShaderTargets[replacementIndex];
		const ModifiedShader::PackageDisk* modifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(replacement.modifiedShaderId);
		replacement.modifiedShaderBlobPath = modifiedShader ? modifiedShader->compiledBlobPath : "";

		if (!modifiedShader)
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseShaderTargets->CompileShaderTarget: ModifiedShader package not found: " + replacement.modifiedShaderId);
			return false;
		}

		if (!modifiedShader->enabled || modifiedShader->shaderType != replacement.shaderType)
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseShaderTargets->CompileShaderTarget: package is disabled or has the wrong shader type: " + modifiedShader->id);
			return false;
		}

		if (modifiedShader->sourcePath.empty() || !ShaderInjectorIO::FileExists(modifiedShader->sourcePath))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseShaderTargets->CompileShaderTarget: package source file not found: " + modifiedShader->sourcePath);
			return false;
		}

		if (!DatabaseModifiedShaders::CompileModifiedShader(modifiedShader->id))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseShaderTargets->CompileShaderTarget: compile failed for " + modifiedShader->sourcePath);
			return false;
		}

		replacement.modifiedShaderBlobPath = modifiedShader->compiledBlobPath;
		ShaderInjectorGUI::WriteToRuntimeLogSuccess("DatabaseShaderTargets->CompileShaderTarget: Compiled ModifiedShader: " + modifiedShader->id + " -> " + modifiedShader->compiledBlobPath);
		return true;
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| RELOAD SHADER TARGETS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| RELOAD SHADER TARGETS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| RELOAD SHADER TARGETS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//using the selected index of currently selected shader replacement...
	//"reload" the shader replacement by loading compiled shader blobs from disk
	bool ReloadShaderTarget(int replacementIndex)
	{
		if (replacementIndex < 0 || replacementIndex >= (int)gLoadedShaderTargets.size())
			return false;

		ShaderTarget::ShaderTargetDisk& replacement = gLoadedShaderTargets[replacementIndex];
		const ModifiedShader::PackageDisk* modifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(replacement.modifiedShaderId);
		replacement.modifiedShaderBlobPath = modifiedShader ? modifiedShader->compiledBlobPath : "";

		if (replacementIndex >= (int)gLoadedShaderTargetBlobs.size())
			gLoadedShaderTargetBlobs.resize(gLoadedShaderTargets.size());

		std::vector<uint8_t> compiledReplacementBlob;

		if (modifiedShader && IsShaderTargetEffectivelyEnabled(replacement))
			compiledReplacementBlob = modifiedShader->compiledBlob;

		if (!modifiedShader || compiledReplacementBlob.empty())
		{
			gLoadedShaderTargetBlobs[replacementIndex].clear();
			ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseShaderTargets->ReloadShaderTarget: failed to load compiled blob for ModifiedShader " + replacement.modifiedShaderId);
		}
		else
		{
			gLoadedShaderTargetBlobs[replacementIndex] = compiledReplacementBlob;
			ShaderInjectorGUI::WriteToRuntimeLog("DatabaseShaderTargets->ReloadShaderTarget: Reloaded shader replacement: " + replacement.name + " bytes=" + std::to_string(gLoadedShaderTargetBlobs[replacementIndex].size()));
		}

		//IMPORTANT NOTE: brackets are important here, we need to limit the scope when collecting the root signature
		{
			//a newly loaded blob invalidates all rebuilt replacement PSOs; they must be recreated with the new bytecode.
			std::lock_guard<std::mutex> lock(gPipelineMutex);
			InvalidateAllReplacementPSOs();
		}

		//IMPORTANT: let the rest of the injector know that our shader replacement is dirty (needs to be updated)
		MarkShaderTargetApplyDirty();
		ShaderDiscovery::ResetRuntimeCache();
		return !gLoadedShaderTargetBlobs[replacementIndex].empty();
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SAVE SHADER REPLACEMENT |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SAVE SHADER REPLACEMENT |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SAVE SHADER REPLACEMENT |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//using the selected index of currently selected shader replacement...
	//save any changes made to the shader replacement to the disk
	bool SaveShaderTarget(int replacementIndex)
	{
		if (replacementIndex < 0 || replacementIndex >= (int)gLoadedShaderTargets.size())
			return false;

		ShaderTarget::ShaderTargetDisk& replacement = gLoadedShaderTargets[replacementIndex];
		replacement.name = gShaderTargetNameBuffer;

		if (!ShaderTarget::WriteShaderTargetJson(replacement))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseShaderTargets->SaveShaderTarget: failed to save " + replacement.jsonPath);
			return false;
		}

		//IMPORTANT: let the rest of the injector know that our shader replacement is dirty (needs to be updated)
		MarkShaderTargetApplyDirty();
		ShaderDiscovery::ResetRuntimeCache();

		ShaderInjectorGUI::WriteToRuntimeLog("DatabaseShaderTargets->SaveShaderTarget: Saved shader replacement: " + replacement.jsonPath);
		return true;
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| DELETE SHADER REPLACEMENT |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| DELETE SHADER REPLACEMENT |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| DELETE SHADER REPLACEMENT |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//using the selected index of currently selected shader replacement...
	//delete a shader replacement from the disk (and memory)!
	bool DeleteShaderTarget(int replacementIndex)
	{
		if (replacementIndex < 0 || replacementIndex >= (int)gLoadedShaderTargets.size())
			return false;

		ShaderTarget::ShaderTargetDisk replacement = gLoadedShaderTargets[replacementIndex];

		ShaderInjectorIO::DeleteFileIfExists(replacement.originalShaderBlobPath);
		ShaderInjectorIO::DeleteFileIfExists(replacement.jsonPath);

		if (!replacement.replacementDirectory.empty() && ShaderInjectorIO::DirectoryExists(replacement.replacementDirectory))
			RemoveDirectoryA(replacement.replacementDirectory.c_str());

		//IMPORTANT NOTE: brackets are important here, we need to limit the scope when collecting the root signature
		{
			std::lock_guard<std::mutex> lock(gPipelineMutex);
			InvalidateAllReplacementPSOs();
		}

		ShaderInjectorGUI::WriteToRuntimeLog("DatabaseShaderTargets->DeleteShaderTarget: Deleted shader replacement: " + replacement.name);
		
		//we need to refresh the list so we are up to date with the shader targets that exist in the folder
		RefreshLoadedShaderTargets();
		
		//IMPORTANT: let the rest of the injector know that our shader replacement is dirty (needs to be updated)
		MarkShaderTargetApplyDirty();

		return true;
	}

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| REFRESH SHADER TARGETS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| REFRESH SHADER TARGETS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| REFRESH SHADER TARGETS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//recollect shader targets from the shader replacement folder, and load them into memory
	void RefreshLoadedShaderTargets()
	{
		//rebuild the runtime list from disk so external edits or deleted replacement folders are reflected in the UI.
		ResetCachedBlobContentLookup();
		ShaderDiscovery::ResetRuntimeCache();
		gLoadedShaderTargets.clear();
		gLoadedShaderTargetBlobs.clear();
		gSelectedShaderTargetIndex = -1;
		gShaderTargetNameBufferIndex = -1;
		gShaderTargetNameBuffer[0] = '\0';

		const std::string replacementDirectory = ShaderInjectorIO::GetShaderTargetsDirectory();
		DatabaseModifiedShaders::EnsureModifiedShadersLoaded();

		if (!ShaderInjectorIO::DirectoryExists(replacementDirectory))
			ShaderInjectorIO::DirectoryCreate(replacementDirectory);

		std::vector<std::string> replacementJsonPaths;
		ShaderTarget::CollectShaderTargetJsonFiles(replacementDirectory, replacementJsonPaths);
		std::sort(replacementJsonPaths.begin(), replacementJsonPaths.end());

		for (const std::string& replacementJsonPath : replacementJsonPaths)
		{
			ShaderTarget::ShaderTargetDisk replacement{};

			if (ShaderTarget::LoadShaderTargetJson(replacementJsonPath, replacement))
			{
				BackfillReplacementPortableMetadataFromSidecars(replacement);

				if (Globals::gShaderDiscoveryMode == Globals::ShaderDiscoveryMode::ShaderAnalysis)
					ShaderDiscovery::EnsureReplacementAnalysis(replacement);

				std::vector<uint8_t> compiledReplacementBlob;

				const ModifiedShader::PackageDisk* modifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(replacement.modifiedShaderId);
				replacement.modifiedShaderBlobPath = modifiedShader ? modifiedShader->compiledBlobPath : "";

				if (modifiedShader && IsShaderTargetEffectivelyEnabled(replacement))
					compiledReplacementBlob = modifiedShader->compiledBlob;

				gLoadedShaderTargets.push_back(replacement);
				gLoadedShaderTargetBlobs.push_back(compiledReplacementBlob);
			}
		}

		if (!gLoadedShaderTargets.empty())
			gSelectedShaderTargetIndex = 0;

		ResetUncapturedReplacementAttempts();
		gLoadedShaderTargetsOnce = true;

		//IMPORTANT: let the rest of the injector know that our shader replacement is dirty (needs to be updated)
		MarkShaderTargetApplyDirty();

		ShaderInjectorGUI::WriteToRuntimeLog("DatabaseShaderTargets->RefreshLoadedShaderTargets: Loaded shader targets from disk: " + std::to_string(gLoadedShaderTargets.size()));

		for (const auto& replacement : gLoadedShaderTargets)
		{
			ShaderInjectorGUI::WriteToRuntimeLog(
				"DatabaseShaderTargets->RefreshLoadedShaderTargets: Replacement loaded: " + replacement.name +
				" enabled=" + std::to_string(replacement.enabled ? 1 : 0) +
				" shaderHash=" + replacement.originalShaderBytecodeHash +
				" cacheHash=" + replacement.pipelineCachedBlobHash +
				" cacheBytes=" + replacement.pipelineCachedBlobLength +
				" source=" + replacement.sourceList +
				" index=" + replacement.pipelineIndex);
		}
	}

	//utility function for UI
	void SyncShaderTargetNameBuffer()
	{
		if (gSelectedShaderTargetIndex < 0 || gSelectedShaderTargetIndex >= (int)gLoadedShaderTargets.size())
		{
			gShaderTargetNameBufferIndex = -1;
			gShaderTargetNameBuffer[0] = '\0';
			return;
		}

		if (gShaderTargetNameBufferIndex == gSelectedShaderTargetIndex)
			return;

		const std::string& name = gLoadedShaderTargets[gSelectedShaderTargetIndex].name;
		strncpy_s(gShaderTargetNameBuffer, name.c_str(), _TRUNCATE);
		gShaderTargetNameBufferIndex = gSelectedShaderTargetIndex;
	}
}
