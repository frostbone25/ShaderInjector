#include "ModifiedShaderCreation.h"

#include "DatabaseModifiedShaders.h"
#include "Hash.h"
#include "ModifiedShader.h"
#include "ShaderAnalyzer.h"
#include "ShaderInjectorIO.h"
#include "ShaderTemplates.h"
#include "StringHelper.h"

namespace ModifiedShaderCreation
{
	bool CreateTemplate(
		ShaderTarget::ShaderType shaderType,
		uint64_t shaderHash,
		const void* shaderBytecode,
		size_t shaderBytecodeLength,
		std::string& outMessage)
	{
		if (shaderType == ShaderTarget::Unknown || shaderHash == 0 || !shaderBytecode || shaderBytecodeLength == 0)
		{
			outMessage = "Cannot create a Modified Shader template without valid captured shader bytecode.";
			return false;
		}

		const std::string shaderTypeName = StringHelper::ShaderTypeToString(shaderType);
		const std::string hashText = Hash::FormatHash(shaderHash);
		const std::string packageName = "Modified" + shaderTypeName + "_" + hashText;
		const std::string packageDirectory = ShaderInjectorIO::JoinPath(ShaderInjectorIO::GetModifiedShadersDirectory(), packageName);
		const std::string jsonPath = ShaderInjectorIO::JoinPath(packageDirectory, packageName + ".json");
		const std::string sourceFile = packageName + ".hlsl";
		const std::string sourcePath = ShaderInjectorIO::JoinPath(packageDirectory, sourceFile);
		const std::string compiledBlobFile = packageName + ShaderInjectorIO::extensionBLOB;
		const std::string compiledBlobPath = ShaderInjectorIO::JoinPath(packageDirectory, compiledBlobFile);

		if (ShaderInjectorIO::PathExists(packageDirectory) ||
			ShaderInjectorIO::FileExists(jsonPath) ||
			ShaderInjectorIO::FileExists(sourcePath))
		{
			outMessage = "Modified Shader package already exists: " + packageDirectory;
			return false;
		}

		ModifiedShader::PackageDisk package{};
		package.id = packageName;
		package.name = packageName;
		package.description = "Modified Shader template generated from captured " + shaderTypeName + " bytecode.";
		package.shaderType = shaderType;
		package.shaderProfile = StringHelper::ShaderProfileForType(shaderType);
		package.shaderEntryPoint = "main";
		package.sourceFile = sourceFile;
		package.compiledBlobFile = compiledBlobFile;
		package.packageDirectory = packageDirectory;
		package.jsonPath = jsonPath;
		package.sourcePath = sourcePath;
		package.compiledBlobPath = compiledBlobPath;
		package.notes = "Rename this package and edit its HLSL source as needed. Target metadata is used for automated shader discovery.";

		ModifiedShader::TargetDisk target{};
		target.name = "CapturedShader_" + hashText;
		target.knownShaderBytecodeHashes.push_back(hashText);
		target.originalShaderBytecodeLength = std::to_string(shaderBytecodeLength);
		ShaderAnalyzer::Analyze(shaderBytecode, shaderBytecodeLength, target.shaderAnalysis);
		package.targets.push_back(std::move(target));

		const char* sourceTemplate = ShaderTemplates::GetModifiedShaderSourceTemplate(shaderType);

		if (!sourceTemplate || package.shaderProfile.empty())
		{
			outMessage = "No Modified Shader source template is available for " + shaderTypeName + ".";
			return false;
		}

		ShaderInjectorIO::DirectoryCreate(packageDirectory);

		if (!ShaderInjectorIO::WriteTextFileIfMissing(sourcePath, sourceTemplate))
		{
			outMessage = "Failed to write Modified Shader source: " + sourcePath;
			return false;
		}

		if (!ModifiedShader::WriteJson(package))
		{
			ShaderInjectorIO::DeleteFileIfExists(sourcePath);
			outMessage = "Failed to write Modified Shader metadata: " + jsonPath;
			return false;
		}

		DatabaseModifiedShaders::RefreshModifiedShaders();
		outMessage = "Created Modified Shader template: " + packageDirectory;
		return true;
	}
}
