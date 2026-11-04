#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ModifiedShader.h"
#include "ShaderConfiguration/CompileConfiguration.h"

namespace DatabaseModifiedShaders
{
	struct CompileProgress
	{
		int total = 0;
		int completed = 0;
		int failed = 0;
		int queued = 0;
		int running = 0;
		int priorityTotal = 0;
		int priorityCompleted = 0;
		int priorityFailed = 0;
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
	bool CompileModifiedShader(const std::string& modifiedShaderId, bool includeLinkedRenderPasses = true);

	CompileProgress GetCompileProgress();

	void ProcessCompletedCompiles();

	bool QueueBackgroundCompile(
		std::function<void()> compileTask,
		bool urgent = false,
		std::shared_ptr<ShaderConfiguration::ConfigurationProgress> progress = {});

	void CancelConfigurationCompiles(std::shared_ptr<ShaderConfiguration::ConfigurationProgress> progress);
	bool HasInterruptedCompiles(const std::string& ownerID);
	void MarkInterruptedCompile(const std::string& ownerID);
	void ClearInterruptedCompiles(const std::string& ownerID);

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
		std::vector<ModifiedShader::ModifiedShaderCompiledVariant>& outVariants,
		bool loadCachedBlobs = true);

	void RequestVariantCompile(
		const std::string& ownerId,
		const std::vector<ModifiedShader::ShaderDefine>& defines,
		std::shared_ptr<const ShaderConfiguration::CompileConfiguration> configuration = {});

	//record a target hash when its pipeline is bound so recent runtime variants can compile first.
	void RecordVariantUse(uint64_t shaderHash);

	uint64_t GetVariantUseStamp(uint64_t shaderHash);

	int QueueSourceCompiles(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		const std::string& ownerId,
		const std::string& sourcePath,
		std::vector<ModifiedShader::ModifiedShaderCompiledVariant>& variants,
		std::shared_ptr<const ShaderConfiguration::CompileConfiguration> configuration = {});
} //namespace DatabaseModifiedShaders
