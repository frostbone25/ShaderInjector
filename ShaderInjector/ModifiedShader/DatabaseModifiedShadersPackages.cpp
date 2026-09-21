#include "DatabaseModifiedShaders.h"
#include "DatabaseModifiedShadersInternal.h"

#include <string>

#include "IO/ShaderInjectorIO.h"
#include "StringHelper.h"

namespace DatabaseModifiedShaders
{
	std::string JsonFileNameForStem(const std::string& fileStem)
	{
		return fileStem + "_Fingerprint" + ShaderInjectorIO::extensionJSON;
	}

	std::string SourceFileNameForStem(const std::string& fileStem)
	{
		return fileStem + "_Source" + ShaderInjectorIO::extensionHLSL;
	}

	std::string CompiledBlobFileNameForStem(const std::string& fileStem)
	{
		return fileStem + "_Compiled" + ShaderInjectorIO::extensionBLOB;
	}

	bool MoveExistingFileIfNeeded(const std::string& currentPath, const std::string& desiredPath, bool required)
	{
		if (ShaderInjectorIO::PathsEqual(currentPath, desiredPath))
			return true;

		if (!ShaderInjectorIO::FileExists(currentPath))
			return !required;

		if (ShaderInjectorIO::PathExists(desiredPath))
			return false;

		return ShaderInjectorIO::MovePath(currentPath, desiredPath);
	}

	bool MoveModifiedShaderPackageToName(ModifiedShader::ModifiedShaderPackageDisk& modifiedShader, const std::string& displayName)
	{
		const std::string fileStem = ShaderInjectorIO::SanitizeFileStem(displayName);

		if (fileStem.empty())
			return false;

		const std::string oldDirectory = modifiedShader.packageDirectory;

		if (oldDirectory.empty() || !ShaderInjectorIO::DirectoryExists(oldDirectory))
			return false;

		const std::string oldJsonPath = modifiedShader.jsonPath;
		const std::string oldSourcePath = modifiedShader.sourcePath;
		const std::string oldCompiledBlobPath = modifiedShader.compiledBlobPath;

		const std::string desiredDirectory = ShaderInjectorIO::JoinPath(ShaderInjectorIO::GetModifiedShadersDirectory(), fileStem);
		const bool directoryAlreadyMatches = ShaderInjectorIO::PathsEqual(oldDirectory, desiredDirectory);

		if (!directoryAlreadyMatches)
		{
			//move the package directory first so its files can be renamed in place.
			if (ShaderInjectorIO::PathExists(desiredDirectory))
				return false;

			if (!ShaderInjectorIO::MovePath(oldDirectory, desiredDirectory))
				return false;
		}

		const std::string currentJsonPath = ShaderInjectorIO::JoinPath(desiredDirectory, ShaderInjectorIO::FileNameFromPath(oldJsonPath));
		const std::string currentSourcePath = ShaderInjectorIO::JoinPath(desiredDirectory, ShaderInjectorIO::FileNameFromPath(oldSourcePath));
		const std::string currentCompiledBlobPath = ShaderInjectorIO::JoinPath(desiredDirectory, ShaderInjectorIO::FileNameFromPath(oldCompiledBlobPath));

		const std::string desiredJsonPath = ShaderInjectorIO::JoinPath(desiredDirectory, JsonFileNameForStem(fileStem));
		const std::string desiredSourcePath = ShaderInjectorIO::JoinPath(desiredDirectory, SourceFileNameForStem(fileStem));
		const std::string desiredCompiledBlobPath = ShaderInjectorIO::JoinPath(desiredDirectory, CompiledBlobFileNameForStem(fileStem));

		if (!MoveExistingFileIfNeeded(currentSourcePath, desiredSourcePath, true))
			return false;

		if (!MoveExistingFileIfNeeded(currentCompiledBlobPath, desiredCompiledBlobPath, false))
			return false;

		modifiedShader.name = displayName;
		modifiedShader.packageDirectory = desiredDirectory;
		modifiedShader.sourceFile = ShaderInjectorIO::FileNameFromPath(desiredSourcePath);
		modifiedShader.sourcePath = desiredSourcePath;
		modifiedShader.compiledBlobFile = ShaderInjectorIO::FileNameFromPath(desiredCompiledBlobPath);
		modifiedShader.compiledBlobPath = desiredCompiledBlobPath;
		modifiedShader.jsonPath = desiredJsonPath;

		if (!ShaderInjectorIO::PathsEqual(currentJsonPath, desiredJsonPath) && ShaderInjectorIO::PathExists(desiredJsonPath))
			return false;

		if (!ModifiedShader::WriteJson(modifiedShader))
			return false;

		if (!ShaderInjectorIO::PathsEqual(currentJsonPath, desiredJsonPath))
			ShaderInjectorIO::DeleteFileIfExists(currentJsonPath);

		return true;
	}

	bool SetModifiedShaderEnabled(const std::string& modifiedShaderId, bool enabled)
	{
		ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = Detail::FindMutableModifiedShaderById(modifiedShaderId);

		if (!modifiedShader)
			return false;

		modifiedShader->enabled = enabled;

		if (!ModifiedShader::WriteJson(*modifiedShader))
			return false;

		RefreshModifiedShaders();

		return true;
	}

	bool SetModifiedShaderName(const std::string& modifiedShaderId, const std::string& name)
	{
		const std::string displayName = StringHelper::TrimWhitespace(name);

		if (displayName.empty())
			return false;

		ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = Detail::FindMutableModifiedShaderById(modifiedShaderId);

		if (!modifiedShader || !MoveModifiedShaderPackageToName(*modifiedShader, displayName))
			return false;

		RefreshModifiedShaders();

		return true;
	}

	bool DeleteModifiedShader(const std::string& modifiedShaderId)
	{
		const ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = FindModifiedShaderById(modifiedShaderId);

		if (!modifiedShader || modifiedShader->packageDirectory.empty())
			return false;

		const std::string packageDirectory = modifiedShader->packageDirectory;

		if (!ShaderInjectorIO::DeleteDirectoryRecursively(packageDirectory))
			return false;

		RefreshModifiedShaders();

		return true;
	}
}