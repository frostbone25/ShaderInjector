#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "JsonHelper.h"
#include "ModifiedShader/ModifiedShaderTargetDisk.h"
#include "ShaderTarget/ShaderTarget.h"

namespace ModifiedShader
{
	struct ModifiedShaderPackageDisk
	{
		bool enabled = true;
		std::string id;
		std::string name;
		ShaderTarget::ShaderType shaderType = ShaderTarget::Unknown;
		std::string shaderProfile;
		std::string shaderEntryPoint = "main";
		std::string sourceFile;
		std::string compiledBlobFile;
		std::vector<ModifiedShaderTargetDisk> targets;

		// Runtime-only resolved paths. They are normalized before JSON is written.
		std::string packageDirectory;
		std::string jsonPath;
		std::string sourcePath;
		std::string compiledBlobPath;
		std::vector<uint8_t> compiledBlob;
		ShaderAnalysis::ShaderAnalysisDisk compiledShaderAnalysis;
		bool compiledShaderInterfaceCompatible = true;

		bool MatchesShader(uint64_t shaderHash, const ShaderAnalysis::ShaderAnalysisDisk& analysis) const;
		double CalculateSimilarityScore(const ModifiedShaderPackageDisk& other) const;
		static double CalculateSimilarityScore(const std::vector<ModifiedShaderPackageDisk>& left, const std::vector<ModifiedShaderPackageDisk>& right);

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			ModifiedShaderPackageDisk,
			enabled,
			id,
			name,
			shaderType,
			shaderProfile,
			shaderEntryPoint,
			sourceFile,
			compiledBlobFile,
			targets,
			packageDirectory,
			jsonPath,
			sourcePath,
			compiledBlobPath)
	};
}
