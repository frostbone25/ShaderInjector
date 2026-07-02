#include "DatabaseModifiedShaders.h"

#include <algorithm>
#include <mutex>
#include <unordered_set>

#include "ShaderInjectorIO.h"
#include "ShaderDiscovery.h"
#include "ShaderAutomaticDiscovery.h"

namespace DatabaseModifiedShaders
{
	namespace
	{
		std::vector<ModifiedShader::PackageDisk> gModifiedShaders;
		bool gModifiedShadersLoaded = false;
	}

	void RefreshModifiedShaders()
	{
		ShaderDiscovery::ResetRuntimeCache();
		gModifiedShaders.clear();
		gModifiedShadersLoaded = true;

		const std::string modifiedShadersDirectory = ShaderInjectorIO::GetModifiedShadersDirectory();
		ShaderInjectorIO::DirectoryCreate(modifiedShadersDirectory);

		std::vector<std::string> jsonPaths;
		ShaderInjectorIO::CollectFilesByExtension(
			modifiedShadersDirectory,
			ShaderInjectorIO::extensionJSON,
			jsonPaths,
			true,
			true);
		std::sort(jsonPaths.begin(), jsonPaths.end());

		std::unordered_set<std::string> loadedIds;
		for (const std::string& jsonPath : jsonPaths)
		{
			ModifiedShader::PackageDisk package{};
			if (!ModifiedShader::LoadJson(jsonPath, package))
				continue;

			if (package.id.empty() || package.shaderType == ShaderTarget::Unknown ||
				package.shaderProfile.empty() || package.shaderEntryPoint.empty() ||
				package.sourcePath.empty() || !ShaderInjectorIO::FileExists(package.sourcePath) ||
				package.compiledBlobPath.empty())
			{
				ShaderInjectorIO::WriteToLogFileWarning(
					"DatabaseModifiedShaders->RefreshModifiedShaders: ignoring incomplete package " + jsonPath);
				continue;
			}

			if (!loadedIds.insert(package.id).second)
			{
				ShaderInjectorIO::WriteToLogFileWarning(
					"DatabaseModifiedShaders->RefreshModifiedShaders: duplicate package id " + package.id);
				continue;
			}

			if (ShaderInjectorIO::FileExists(package.compiledBlobPath))
				ShaderInjectorIO::LoadDXILBlobFromDisk(package.compiledBlobPath, package.compiledBlob);

			gModifiedShaders.push_back(std::move(package));
		}

		std::sort(gModifiedShaders.begin(), gModifiedShaders.end(), [](const auto& left, const auto& right)
		{
			const std::string leftName = left.name.empty() ? left.id : left.name;
			const std::string rightName = right.name.empty() ? right.id : right.name;
			return leftName < rightName;
		});

		ShaderAutomaticDiscovery::RefreshModifiedShaderIndex(gModifiedShaders);

		ShaderInjectorIO::WriteToLogFile(
			"DatabaseModifiedShaders->RefreshModifiedShaders: loaded packages=" + std::to_string(gModifiedShaders.size()));
	}

	void EnsureModifiedShadersLoaded()
	{
		if (!gModifiedShadersLoaded)
			RefreshModifiedShaders();
	}

	const std::vector<ModifiedShader::PackageDisk>& GetModifiedShaders()
	{
		EnsureModifiedShadersLoaded();
		return gModifiedShaders;
	}

	const ModifiedShader::PackageDisk* FindModifiedShaderById(const std::string& modifiedShaderId)
	{
		if (modifiedShaderId.empty())
			return nullptr;

		EnsureModifiedShadersLoaded();
		for (const ModifiedShader::PackageDisk& modifiedShader : gModifiedShaders)
		{
			if (modifiedShader.id == modifiedShaderId)
				return &modifiedShader;
		}
		return nullptr;
	}

	std::string DisplayName(const ModifiedShader::PackageDisk& modifiedShader)
	{
		if (modifiedShader.name.empty() || modifiedShader.name == modifiedShader.id)
			return modifiedShader.id;
		return modifiedShader.name + " (" + modifiedShader.id + ")";
	}

	bool SetModifiedShaderEnabled(const std::string& modifiedShaderId, bool enabled)
	{
		EnsureModifiedShadersLoaded();
		for (ModifiedShader::PackageDisk& modifiedShader : gModifiedShaders)
		{
			if (modifiedShader.id != modifiedShaderId)
				continue;

			modifiedShader.enabled = enabled;
			if (!ModifiedShader::WriteJson(modifiedShader))
				return false;

			RefreshModifiedShaders();
			return true;
		}

		return false;
	}

	bool SetModifiedShaderName(const std::string& modifiedShaderId, const std::string& name)
	{
		if (name.empty() || name.find_first_not_of(" \t\r\n") == std::string::npos)
			return false;

		EnsureModifiedShadersLoaded();
		for (ModifiedShader::PackageDisk& modifiedShader : gModifiedShaders)
		{
			if (modifiedShader.id != modifiedShaderId)
				continue;

			modifiedShader.name = name;
			if (!ModifiedShader::WriteJson(modifiedShader))
				return false;

			RefreshModifiedShaders();
			return true;
		}

		return false;
	}

	bool DeleteModifiedShader(const std::string& modifiedShaderId)
	{
		const ModifiedShader::PackageDisk* modifiedShader = FindModifiedShaderById(modifiedShaderId);
		if (!modifiedShader || modifiedShader->packageDirectory.empty())
			return false;

		const std::string packageDirectory = modifiedShader->packageDirectory;
		if (!ShaderInjectorIO::DeleteDirectoryRecursively(packageDirectory))
			return false;

		RefreshModifiedShaders();
		return true;
	}

	bool CompileModifiedShader(const std::string& modifiedShaderId)
	{
		EnsureModifiedShadersLoaded();
		ModifiedShader::PackageDisk* modifiedShader = nullptr;
		for (ModifiedShader::PackageDisk& candidate : gModifiedShaders)
		{
			if (candidate.id == modifiedShaderId)
			{
				modifiedShader = &candidate;
				break;
			}
		}

		if (!modifiedShader || !modifiedShader->enabled || modifiedShader->sourcePath.empty() ||
			modifiedShader->compiledBlobPath.empty())
		{
			return false;
		}

		std::string compiledBlobPath = modifiedShader->compiledBlobPath;
		if (!ShaderInjectorIO::CompileSourceToDXILBlob(
			modifiedShader->sourcePath,
			modifiedShader->shaderProfile,
			modifiedShader->shaderEntryPoint,
			compiledBlobPath))
		{
			return false;
		}

		modifiedShader->compiledBlob.clear();
		return ShaderInjectorIO::LoadDXILBlobFromDisk(compiledBlobPath, modifiedShader->compiledBlob) &&
			!modifiedShader->compiledBlob.empty();
	}
}
