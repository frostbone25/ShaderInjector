#include "DatabaseModifiedShaders.h"
#include "DatabaseModifiedShadersInternal.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>

#include "Globals.h"
#include "IO/ShaderInjectorIO.h"
#include "ShaderDiscovery.h"
#include "ShaderAutomaticDiscovery.h"
#include "StringHelper.h"

namespace DatabaseModifiedShaders
{
	std::vector<ModifiedShader::ModifiedShaderPackageDisk> gModifiedShaders;

	bool gModifiedShadersLoaded = false;

	void RefreshModifiedShaders()
	{
		//package data and discovery indexes must be rebuilt together so a refresh cannot leave discovery using fingerprints from a previous package list.
		ShaderDiscovery::ResetRuntimeCache();
		gModifiedShaders.clear();
		gModifiedShadersLoaded = true;

		const std::string modifiedShadersDirectory = ShaderInjectorIO::GetModifiedShadersDirectory();
		ShaderInjectorIO::DirectoryCreate(modifiedShadersDirectory);

		std::vector<std::string> jsonPaths;

		ShaderInjectorIO::CollectFilesByExtension(modifiedShadersDirectory, ShaderInjectorIO::extensionJSON, jsonPaths, true, true);

		std::sort(jsonPaths.begin(), jsonPaths.end());

		std::unordered_set<std::string> loadedIds;

		for (const std::string& jsonPath : jsonPaths)
		{
			ModifiedShader::ModifiedShaderPackageDisk package{};

			if (!ModifiedShader::LoadJson(jsonPath, package))
				continue;

			//compiler targets are injector-wide settings.
			//override package metadata in memory so existing packages follow the selected profile on recompile.
			package.shaderProfile = StringHelper::ShaderProfileForType(package.shaderType);

			if (package.id.empty() ||
				package.shaderType == ShaderTarget::Unknown ||
				package.shaderProfile.empty() ||
				package.shaderEntryPoint.empty() ||
				package.sourcePath.empty() ||
				!ShaderInjectorIO::FileExists(package.sourcePath) ||
				package.compiledBlobPath.empty())
			{
				ShaderInjectorIO::WriteToLogFileWarning("DatabaseModifiedShaders->RefreshModifiedShaders: ignoring incomplete package " + jsonPath);
				continue;
			}

			//a package ID may appear in multiple JSON files, but only one package can be active under that ID in the runtime database.
			if (!loadedIds.insert(package.id).second)
			{
				ShaderInjectorIO::WriteToLogFileWarning("DatabaseModifiedShaders->RefreshModifiedShaders: duplicate package id " + package.id);
				continue;
			}

			if (ShaderInjectorIO::FileExists(package.compiledBlobPath))
			{
				ShaderInjectorIO::LoadDXILBlobFromDisk(package.compiledBlobPath, package.compiledBlob);
				Detail::AnalyzeCompiledBlob(package.compiledBlob, package.compiledShaderAnalysis);
				package.compiledShaderInterfaceCompatible = Detail::ShaderInterfaceMatchesAnyPackageTarget(package, package.compiledShaderAnalysis);

				if (!package.compiledShaderInterfaceCompatible)
				{
					//repair an existing blob only when its reflected interface disagrees with every analyzed target in this package.
					ShaderInjectorIO::WriteToLogFileWarning("DatabaseModifiedShaders->RefreshModifiedShaders: repairing incompatible compiled interface for " + package.id);
					package.compiledShaderInterfaceCompatible = Detail::CompileModifiedShaderPackage(package);
				}
			}

			gModifiedShaders.push_back(std::move(package));
		}

		std::sort(gModifiedShaders.begin(), gModifiedShaders.end(), [](const auto& left, const auto& right)
				  {
			std::string leftName = left.id;
			std::string rightName = right.id;
			if (!left.name.empty())
				leftName = left.name;
			if (!right.name.empty())
				rightName = right.name;
			return leftName < rightName; });

		ShaderAutomaticDiscovery::RefreshModifiedShaderIndex(gModifiedShaders);

		const std::string countMessage = "DatabaseModifiedShaders->RefreshModifiedShaders: loaded packages=" + std::to_string(gModifiedShaders.size());

		if (gModifiedShaders.empty())
			ShaderInjectorIO::WriteToLogFileWarning(countMessage + " (no modified shaders found)");
		else
			ShaderInjectorIO::WriteToLogFileStatus(countMessage);

		if (Globals::gLogModifiedShaderNames)
		{
			for (const ModifiedShader::ModifiedShaderPackageDisk& package : gModifiedShaders)
			{
				std::string displayName = package.id;
				if (!package.name.empty())
					displayName = package.name;
				ShaderInjectorIO::WriteToLogFileStatus("DatabaseModifiedShaders->RefreshModifiedShaders: loaded name=\"" + displayName + "\" id=" + package.id);
			}
		}
	}

	void EnsureModifiedShadersLoaded()
	{
		if (!gModifiedShadersLoaded)
			RefreshModifiedShaders();
	}

	const std::vector<ModifiedShader::ModifiedShaderPackageDisk>& GetModifiedShaders()
	{
		EnsureModifiedShadersLoaded();
		return gModifiedShaders;
	}

	const ModifiedShader::ModifiedShaderPackageDisk* FindModifiedShaderById(const std::string& modifiedShaderId)
	{
		return Detail::FindMutableModifiedShaderById(modifiedShaderId);
	}

	ModifiedShader::ModifiedShaderPackageDisk* Detail::FindMutableModifiedShaderById(const std::string& modifiedShaderId)
	{
		if (modifiedShaderId.empty())
			return nullptr;

		EnsureModifiedShadersLoaded();

		for (ModifiedShader::ModifiedShaderPackageDisk& modifiedShader : gModifiedShaders)
		{
			if (modifiedShader.id == modifiedShaderId)
				return &modifiedShader;
		}

		return nullptr;
	}

	std::string DisplayName(const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader)
	{
		if (modifiedShader.name.empty() || modifiedShader.name == modifiedShader.id)
			return modifiedShader.id;

		return modifiedShader.name + " (" + modifiedShader.id + ")";
	}
} //namespace DatabaseModifiedShaders
