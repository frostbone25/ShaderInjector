#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ModifiedShader.h"

namespace DatabaseModifiedShaders::Detail
{
	ModifiedShader::ModifiedShaderPackageDisk* FindMutableModifiedShaderById(const std::string& modifiedShaderId);

	bool AnalyzeCompiledBlob(const std::vector<uint8_t>& compiledBlob, ShaderAnalysis::ShaderAnalysisDisk& outAnalysis);

	bool ShaderInterfaceMatchesAnyPackageTarget(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		const ShaderAnalysis::ShaderAnalysisDisk& candidateAnalysis);

	bool CompileModifiedShaderPackage(ModifiedShader::ModifiedShaderPackageDisk& modifiedShader);
} //namespace DatabaseModifiedShaders::Detail
