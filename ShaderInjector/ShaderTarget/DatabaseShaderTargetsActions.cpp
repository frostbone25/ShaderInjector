//Shader target actions that change package blobs or persisted targets.
#include <mutex>
#include <string>
#include <vector>

#include "DatabaseShaderTargets.h"
#include "ModifiedShader/DatabaseModifiedShaders.h"
#include "HookD3D12.h"
#include "ShaderDiscovery.h"
#include "GUI/ShaderInjectorGUI.h"
#include "IO/ShaderInjectorIO.h"
#include "ShaderTarget.h"

namespace HookD3D12
{

	//using the selected index of currently selected shader replacement...
	//find the source HLSL shader file it is pointing to and compile it into a ready to use shader bytecode blob
	bool CompileShaderTarget(int replacementIndex)
	{
		if (replacementIndex < 0 || replacementIndex >= (int)gLoadedShaderTargets.size())
			return false;

		ShaderTarget::ShaderTargetDisk& replacement = gLoadedShaderTargets[replacementIndex];
		const ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(replacement.modifiedShaderId);
		replacement.modifiedShaderBlobPath.clear();
		if (modifiedShader)
			replacement.modifiedShaderBlobPath = modifiedShader->compiledBlobPath;

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

	//using the selected index of currently selected shader replacement...
	//"reload" the shader replacement by loading compiled shader blobs from disk
	bool ReloadShaderTarget(int replacementIndex)
	{
		if (replacementIndex < 0 || replacementIndex >= (int)gLoadedShaderTargets.size())
			return false;

		ShaderTarget::ShaderTargetDisk& replacement = gLoadedShaderTargets[replacementIndex];
		const ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = DatabaseModifiedShaders::FindModifiedShaderById(replacement.modifiedShaderId);
		replacement.modifiedShaderBlobPath.clear();
		if (modifiedShader)
			replacement.modifiedShaderBlobPath = modifiedShader->compiledBlobPath;

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

} //namespace HookD3D12
