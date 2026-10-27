#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "JsonHelper.h"
#include "ModifiedShader/ModifiedShaderTargetDisk.h"
#include "ShaderTarget/ShaderTarget.h"

namespace ModifiedShader
{
	//runtime only: one blob per distinct target define set. a package without defines has exactly one.
	struct ModifiedShaderCompiledVariant
	{
		std::vector<ShaderDefine> defines;
		std::string compiledBlobPath;
		std::vector<uint8_t> compiledBlob;
		ShaderAnalysis::ShaderAnalysisDisk compiledShaderAnalysis;
		bool compiledShaderInterfaceCompatible = false;
		//non-zero while a compile is queued or running; only the newest request's result is kept.
		uint64_t pendingCompileSerial = 0;
		//the previous blob stays in use and lookups do not requeue it.
		bool compileFailed = false;
	};

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
		//extra DXC arguments for this package (ignored by the Shader Model 5 compiler).
		std::vector<std::string> compilerArguments;
		std::vector<ModifiedShaderTargetDisk> targets;

		//Runtime-only resolved paths. They are normalized before JSON is written.
		std::string packageDirectory;
		std::string jsonPath;
		std::string sourcePath;
		std::string compiledBlobPath;
		std::vector<uint8_t> compiledBlob;
		ShaderAnalysis::ShaderAnalysisDisk compiledShaderAnalysis;
		bool compiledShaderInterfaceCompatible = true;

		//compiledBlob/compiledShaderAnalysis above mirror the first ready variant for package-level "is compiled" checks.
		std::vector<ModifiedShaderCompiledVariant> compiledVariants;
		//original shader hash -> compiledVariants index; rebuilt by RebuildVariantIndex.
		std::unordered_map<uint64_t, size_t> variantIndexByShaderHash;
		size_t defaultVariantIndex = SIZE_MAX;

		bool MatchesShader(uint64_t shaderHash, const ShaderAnalysis::ShaderAnalysisDisk& analysis) const;

		void RebuildVariantIndex();

		const ModifiedShaderCompiledVariant* FindVariantForDefines(const std::vector<ShaderDefine>& defines) const;

		//never keep the returned pointer across a compile result; it can be replaced.
		ModifiedShaderCompiledVariant* FindVariantForDefinesMutable(const std::vector<ShaderDefine>& defines);

		//fingerprint matches in these packages cannot fall back to the default variant.
		bool HasPerTargetDefines() const;

		//shaders matched without a hash-listed target (fingerprint matches) use the variant without defines.
		const ModifiedShaderCompiledVariant* FindVariantForShader(
			const std::string& originalShaderBytecodeHash,
			const std::vector<std::string>& shaderBytecodeHashAliases) const;
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
			compilerArguments,
			targets,
			packageDirectory,
			jsonPath,
			sourcePath,
			compiledBlobPath)
	};
} //namespace ModifiedShader
