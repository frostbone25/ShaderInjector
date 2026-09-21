//DatabaseShaderTargets.cpp
#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

//custom
#include "DatabaseShaderTargets.h"
#include "ModifiedShader/DatabaseModifiedShaders.h"
#include "Globals.h"
#include "HookD3D12ReplacementTemplates.h"
#include "HookD3D12ReplacementLookup.h"
#include "ShaderDiscovery.h"
#include "GUI/ShaderInjectorGUI.h"
#include "IO/ShaderInjectorIO.h"
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

		const ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(replacement.modifiedShaderId);

		return modifiedShader && modifiedShader->enabled &&
			modifiedShader->shaderType == replacement.shaderType &&
			DatabaseModifiedShaders::CompiledShaderMatchesTargetInterface(*modifiedShader, replacement.originalShaderAnalysis);
	}

	void RefreshShaderTargetsForModifiedShaderStateChange()
	{
		//existing replacement PSOs were built from the previous effective state.
		//retire them before reloading linked blobs so the next apply pass either rebuilds from the enabled package or falls back to the original game PSO.
		{
			std::lock_guard<std::mutex> lock(gPipelineMutex);
			InvalidateAllReplacementPSOs();
		}

		RefreshLoadedShaderTargets();
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

				const ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(replacement.modifiedShaderId);
				replacement.modifiedShaderBlobPath = modifiedShader ? modifiedShader->compiledBlobPath : "";

				if (modifiedShader && IsShaderTargetEffectivelyEnabled(replacement))
					compiledReplacementBlob = modifiedShader->compiledBlob;
				else if (modifiedShader && modifiedShader->enabled && !modifiedShader->compiledBlob.empty() &&
					!DatabaseModifiedShaders::CompiledShaderMatchesTargetInterface(*modifiedShader, replacement.originalShaderAnalysis))
				{
					ShaderInjectorIO::WriteToLogFileError("DatabaseShaderTargets->RefreshLoadedShaderTargets: refusing incompatible compiled shader interface for " + replacement.name + " from " + modifiedShader->id);
				}

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
