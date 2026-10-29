#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "ModifiedShader/ShaderDefineDisk.h"
#include "ModifiedShader.h"

namespace DatabaseModifiedShaders::Detail
{
	ModifiedShader::ModifiedShaderPackageDisk* FindMutableModifiedShaderById(const std::string& modifiedShaderId);

	bool AnalyzeCompiledBlob(const std::vector<uint8_t>& compiledBlob, ShaderAnalysis::ShaderAnalysisDisk& outAnalysis);

	bool ShaderInterfaceLayoutsMatch(
		const ShaderAnalysis::ShaderAnalysisDisk& expected,
		const ShaderAnalysis::ShaderAnalysisDisk& candidate);

	//a define filter limits the comparison to the targets that share one variant's define set.
	bool ShaderInterfaceMatchesAnyPackageTarget(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		const ShaderAnalysis::ShaderAnalysisDisk& candidateAnalysis,
		const std::vector<ModifiedShader::ShaderDefine>* defineFilter = nullptr);

	//guards compiledVariants and the compile queue. lock order: gPipelineMutex, then this.
	extern std::mutex gCompileMutex;

	void UpdatePackageCompiledState(ModifiedShader::ModifiedShaderPackageDisk& modifiedShader);

	void LoadPackageVariants(ModifiedShader::ModifiedShaderPackageDisk& modifiedShader, bool preserveLoadedBlobs = false);

	std::string DescribeDefines(const std::vector<ModifiedShader::ShaderDefine>& defines);

	std::string FormatDefine(const ModifiedShader::ShaderDefine& define);

	std::string VariantBlobPath(const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader, const std::vector<ModifiedShader::ShaderDefine>& defines);

	bool CompileVariant(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		const std::vector<ModifiedShader::ShaderDefine>& defines,
		const std::string& candidateBasePath,
		std::vector<uint8_t>& outBlob,
		ShaderAnalysis::ShaderAnalysisDisk& outAnalysis);

	int QueuePackageCompiles(ModifiedShader::ModifiedShaderPackageDisk& modifiedShader);
} //namespace DatabaseModifiedShaders::Detail
