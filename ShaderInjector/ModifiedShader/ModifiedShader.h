#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "JsonHelper.h"
#include "ShaderAnalysis.h"
#include "ShaderTarget/ShaderTarget.h"

namespace ModifiedShader
{
	inline constexpr const char* formatName = "ShaderInjector.ModifiedShader";

	struct TargetDisk
	{
		std::string name;
		std::string targetApplication;
		std::string gameVersion;
		std::vector<std::string> knownShaderBytecodeHashes;
		std::string originalShaderBytecodeLength;
		ShaderAnalysis::ShaderAnalysisDisk shaderAnalysis;

		bool MatchesShader(uint64_t shaderHash, const ShaderAnalysis::ShaderAnalysisDisk& analysis) const;
		double CalculateSimilarityScore(const TargetDisk& other) const;
		static double CalculateSimilarityScore(const std::vector<TargetDisk>& left, const std::vector<TargetDisk>& right);

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			TargetDisk,
			name,
			targetApplication,
			gameVersion,
			knownShaderBytecodeHashes,
			originalShaderBytecodeLength,
			shaderAnalysis)
	};

	struct PackageDisk
	{
		int schemaVersion = 2;
		std::string format = formatName;
		bool enabled = true;
		std::string id;
		std::string name;
		std::string author;
		std::string description;
		ShaderTarget::ShaderType shaderType = ShaderTarget::Unknown;
		std::string shaderProfile;
		std::string shaderEntryPoint = "main";
		std::string sourceFile;
		std::string compiledBlobFile;
		std::vector<TargetDisk> targets;
		std::string notes;

		// Runtime-only resolved paths. They are normalized before JSON is written.
		std::string packageDirectory;
		std::string jsonPath;
		std::string sourcePath;
		std::string compiledBlobPath;
		std::vector<uint8_t> compiledBlob;

		bool MatchesShader(uint64_t shaderHash, const ShaderAnalysis::ShaderAnalysisDisk& analysis) const;
		double CalculateSimilarityScore(const PackageDisk& other) const;
		static double CalculateSimilarityScore(const std::vector<PackageDisk>& left, const std::vector<PackageDisk>& right);

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			PackageDisk,
			schemaVersion,
			format,
			enabled,
			id,
			name,
			author,
			description,
			shaderType,
			shaderProfile,
			shaderEntryPoint,
			sourceFile,
			compiledBlobFile,
			targets,
			notes,
			packageDirectory,
			jsonPath,
			sourcePath,
			compiledBlobPath)
	};

	bool WriteJson(const PackageDisk& package);
	bool LoadJson(const std::string& jsonPath, PackageDisk& outPackage);
	TargetDisk BuildTargetFromShaderTarget(
		const ShaderTarget::ShaderTargetDisk& shaderTarget,
		const std::string& targetApplication = "",
		const std::string& gameVersion = "");
}
