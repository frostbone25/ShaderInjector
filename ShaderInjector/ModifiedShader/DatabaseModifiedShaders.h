#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ModifiedShader.h"

namespace DatabaseModifiedShaders
{
	//the overlay draws this while the queue drains; a new batch starts once it is empty.
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

	//current blobs stay in use until their replacement is ready. this is an explicit request, so it
	//also retries a variant whose last compile failed; false when no job could be queued.
	bool CompileModifiedShader(const std::string& modifiedShaderId);

	CompileProgress GetCompileProgress();

	//render thread only: applies finished compiles and hot-swaps the blobs into loaded shader targets.
	void ProcessCompletedCompiles();

	//workers must be joined before hook resources are released.
	void Shutdown();

	bool HasCompiledVariant(const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader);

	bool RecaptureModifiedShader(const std::string& modifiedShaderId, std::string& outMessage, uint64_t selectedShaderHash = 0);

	//false until the variant is compiled; a lookup may queue a background compile for a missing variant.
	bool CompiledShaderMatchesTargetInterface(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		const ShaderAnalysis::ShaderAnalysisDisk& targetAnalysis,
		const std::string& originalShaderBytecodeHash,
		const std::vector<std::string>& shaderBytecodeHashAliases);

	//a copy keeps callers from holding variant state that a recompile can replace.
	bool TryGetCompiledBlobForShader(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		const std::string& originalShaderBytecodeHash,
		const std::vector<std::string>& shaderBytecodeHashAliases,
		std::vector<uint8_t>& outBlob);
} //namespace DatabaseModifiedShaders
