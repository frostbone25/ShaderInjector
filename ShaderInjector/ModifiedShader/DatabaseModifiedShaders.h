#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ModifiedShader.h"

namespace DatabaseModifiedShaders
{
	struct CompileProgress
	{
		int total = 0;
		int completed = 0;
		int failed = 0;
		int queued = 0;
		int running = 0;
	};

	void RefreshModifiedShaders();

	void EnsureModifiedShadersLoaded();

	const std::vector<ModifiedShader::ModifiedShaderPackageDisk>& GetModifiedShaders();

	const ModifiedShader::ModifiedShaderPackageDisk* FindModifiedShaderById(const std::string& modifiedShaderId);

	std::string DisplayName(const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader);

	bool SetModifiedShaderEnabled(const std::string& modifiedShaderId, bool enabled);

	bool SetModifiedShaderName(const std::string& modifiedShaderId, const std::string& name);

	bool DeleteModifiedShader(const std::string& modifiedShaderId);

	//current blobs stay in use until their replacement is ready.
	bool CompileModifiedShader(const std::string& modifiedShaderId);

	CompileProgress GetCompileProgress();

	void ProcessCompletedCompiles();

	void Shutdown();

	bool HasCompiledVariant(const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader);

	bool RecaptureModifiedShader(const std::string& modifiedShaderId, std::string& outMessage, uint64_t selectedShaderHash = 0);

	bool CompiledShaderMatchesTargetInterface(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		const ShaderAnalysis::ShaderAnalysisDisk& targetAnalysis,
		const std::string& originalShaderBytecodeHash,
		const std::vector<std::string>& shaderBytecodeHashAliases);

	bool TryGetCompiledBlobForShader(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		const std::string& originalShaderBytecodeHash,
		const std::vector<std::string>& shaderBytecodeHashAliases,
		std::vector<uint8_t>& outBlob);

	bool TryGetVariantDefinesForShader(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		const std::string& originalShaderBytecodeHash,
		const std::vector<std::string>& shaderBytecodeHashAliases,
		std::vector<ModifiedShader::ShaderDefine>& outDefines);

	void LoadSourceVariants(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		const std::string& sourcePath,
		const std::string& compiledBlobPath,
		std::vector<ModifiedShader::ModifiedShaderCompiledVariant>& outVariants);

	void RequestVariantCompile(const std::string& ownerId, const std::vector<ModifiedShader::ShaderDefine>& defines);

	int QueueSourceCompiles(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		const std::string& ownerId,
		const std::string& sourcePath,
		std::vector<ModifiedShader::ModifiedShaderCompiledVariant>& variants);
} //namespace DatabaseModifiedShaders
