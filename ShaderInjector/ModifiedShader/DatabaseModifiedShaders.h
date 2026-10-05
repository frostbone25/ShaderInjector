#pragma once

#include <string>
#include <vector>

#include "ModifiedShader.h"

namespace DatabaseModifiedShaders
{
	void RefreshModifiedShaders();

	void EnsureModifiedShadersLoaded();

	const std::vector<ModifiedShader::ModifiedShaderPackageDisk>& GetModifiedShaders();

	const ModifiedShader::ModifiedShaderPackageDisk* FindModifiedShaderById(const std::string& modifiedShaderId);

	std::string DisplayName(const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader);

	bool SetModifiedShaderEnabled(const std::string& modifiedShaderId, bool enabled);

	bool SetModifiedShaderName(const std::string& modifiedShaderId, const std::string& name);

	bool DeleteModifiedShader(const std::string& modifiedShaderId);

	bool CompileModifiedShader(const std::string& modifiedShaderId);

	//rebuild fingerprint and linked PSO metadata from original captures; a selected hash can add a new game-version reference.
	//this action never writes or compiles the package's HLSL or replacement blob.
	bool RecaptureModifiedShader(const std::string& modifiedShaderId, std::string& outMessage, uint64_t selectedShaderHash = 0);

	bool CompiledShaderMatchesTargetInterface(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		const ShaderAnalysis::ShaderAnalysisDisk& targetAnalysis);
} //namespace DatabaseModifiedShaders
