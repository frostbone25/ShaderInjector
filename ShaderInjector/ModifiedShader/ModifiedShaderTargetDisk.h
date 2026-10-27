#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "JsonHelper.h"
#include "ModifiedShader/ShaderDefineDisk.h"
#include "ShaderAnalysis/ShaderAnalysis.h"

namespace ModifiedShader
{
	struct ModifiedShaderTargetDisk
	{
		std::string name;
		std::string targetApplication;
		std::string gameVersion;
		std::vector<std::string> knownShaderBytecodeHashes;
		std::string originalShaderBytecodeLength;
		ShaderAnalysis::ShaderAnalysisDisk shaderAnalysis;
		//an empty list uses the package's shared blob; otherwise this target gets its own compiled blob.
		std::vector<ShaderDefine> defines;

		bool MatchesShader(uint64_t shaderHash, const ShaderAnalysis::ShaderAnalysisDisk& analysis) const;
		double CalculateSimilarityScore(const ModifiedShaderTargetDisk& other) const;
		static double CalculateSimilarityScore(const std::vector<ModifiedShaderTargetDisk>& left, const std::vector<ModifiedShaderTargetDisk>& right);

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			ModifiedShaderTargetDisk,
			name,
			targetApplication,
			gameVersion,
			knownShaderBytecodeHashes,
			originalShaderBytecodeLength,
			shaderAnalysis,
			defines)
	};
} //namespace ModifiedShader
