#include "IO/ShaderInjectorIO.h"
#include "ModifiedShader/DatabaseModifiedShaders.h"
#include "ModifiedShader/ModifiedShaderPackageDisk.h"
#include "RenderPass/DatabaseRenderPasses.h"
#include "RenderPass/RenderPass.h"
#include "RenderPass/RenderPassShaders.h"
#include "ShaderResource/DatabaseShaderResources.h"
#include "ShaderResource/ShaderResourceCatalog.h"
#include "StringHelper.h"
#include "ShaderConfiguration/CompileConfiguration.h"

#include <iostream>
#include <unordered_map>
#include <string>
#include <vector>

namespace
{
	struct CompileRequest
	{
		std::string sourcePath;
		std::string profile;
		std::string entryPoint;
		std::string outputPath;
		std::vector<std::string> arguments;
	};

	std::vector<CompileRequest> gCompileRequests;
	bool gHasTemplate = true;
	bool gLoadSucceeds = true;
	std::string gFailSourcePath;
	int gLoadCallCount = 0;
	int gFailureCount = 0;
	std::shared_ptr<const ShaderConfiguration::CompileConfiguration> gExpectedConfiguration;
	bool gConfigurationMatched = true;
	std::unordered_map<std::string, std::string> gWrittenFiles;

	bool Check(bool condition, const std::string& message)
	{
		if (condition)
			return true;

		std::cerr << "FAIL: " << message << '\n';
		++gFailureCount;
		return false;
	}

	void ResetFakes()
	{
		gCompileRequests.clear();
		gHasTemplate = true;
		gLoadSucceeds = true;
		gFailSourcePath.clear();
		gLoadCallCount = 0;
		gExpectedConfiguration.reset();
		gConfigurationMatched = true;
		gWrittenFiles.clear();
	}

	RenderPass::RenderPassDisk MakeFullscreenPass()
	{
		RenderPass::RenderPassDisk renderPass{};
		renderPass.vertexShaderSourcePath = "stage/fullscreen.hlsl";
		renderPass.vertexShaderCompiledBlobPath = "stage/fullscreen.blob";
		renderPass.vertexShaderProfile = "vs_frozen";
		renderPass.vertexShaderEntryPoint = "vertexMain";
		renderPass.fragmentShaderSourcePath = "stage/fragment.hlsl";
		renderPass.fragmentShaderCompiledBlobPath = "stage/fragment.blob";
		renderPass.fragmentShaderProfile = "ps_frozen";
		renderPass.fragmentShaderEntryPoint = "pixelMain";
		return renderPass;
	}

	void TestSelectiveStageCompilation()
	{
		ResetFakes();
		RenderPass::RenderPassDisk renderPass = MakeFullscreenPass();
		renderPass.vertexShaderBlob = {11};
		renderPass.fragmentShaderBlob = {22};
		ShaderConfiguration::DocumentDisk document;
		ShaderConfiguration::PropertyDisk vertexProperty;
		vertexProperty.sourcePath = renderPass.vertexShaderSourcePath;
		vertexProperty.value = "stable";
		document.properties.push_back(vertexProperty);
		ShaderConfiguration::PropertyDisk fragmentProperty;
		fragmentProperty.sourcePath = renderPass.fragmentShaderSourcePath;
		fragmentProperty.value = "before";
		document.properties.push_back(fragmentProperty);
		const auto previousConfiguration = std::make_shared<const ShaderConfiguration::CompileConfiguration>(document);
		document.properties.back().value = "after";
		const auto configuration = std::make_shared<const ShaderConfiguration::CompileConfiguration>(document);
		gExpectedConfiguration = configuration;
		std::string error;
		Check(RenderPassShaders::CompilePreparedShaders(renderPass, {}, error, configuration, previousConfiguration), "selective pass update succeeds");
		Check(gCompileRequests.size() == 1 && gCompileRequests.front().sourcePath == renderPass.fragmentShaderSourcePath, "fragment-only property edit compiles only the fragment stage");
		Check(gWrittenFiles[renderPass.vertexShaderCompiledBlobPath] == std::string(1, 11), "unchanged vertex bytecode is staged for atomic publication");

		gCompileRequests.clear();
		Check(RenderPassShaders::CompilePreparedShaders(renderPass, {}, error, configuration), "force update succeeds without previous selection");
		Check(gCompileRequests.size() == 2, "force update compiles both stages");
	}

	void TestFullscreenPassUsesPreparedValues()
	{
		ResetFakes();
		RenderPass::RenderPassDisk renderPass = MakeFullscreenPass();
		const std::vector<std::string> compilerArguments = {"-enable-16bit-types", "-Wno-foo"};
		std::string error;

		const bool succeeded = RenderPassShaders::CompilePreparedShaders(renderPass, compilerArguments, error);

		Check(succeeded, "fullscreen prepared compile should succeed");
		Check(gCompileRequests.size() == 2, "fullscreen compile should submit vertex and fragment stages");
		if (gCompileRequests.size() == 2)
		{
			Check(gCompileRequests[0].sourcePath == "stage/fullscreen.hlsl", "vertex source path should be copied from prepared pass");
			Check(gCompileRequests[0].profile == "vs_frozen", "vertex profile should remain frozen");
			Check(gCompileRequests[0].entryPoint == "vertexMain", "vertex entry point should remain frozen");
			Check(gCompileRequests[0].outputPath == "stage/fullscreen.blob", "vertex output path should be staged path");
			Check(gCompileRequests[0].arguments.empty(), "linked arguments should not reach vertex compilation");
			Check(gCompileRequests[1].sourcePath == "stage/fragment.hlsl", "fragment source path should be copied from prepared pass");
			Check(gCompileRequests[1].profile == "ps_frozen", "fragment profile should remain frozen");
			Check(gCompileRequests[1].entryPoint == "pixelMain", "fragment entry point should remain frozen");
			Check(gCompileRequests[1].outputPath == "stage/fragment.blob", "fragment output path should be staged path");
			Check(gCompileRequests[1].arguments == compilerArguments, "copied linked arguments should reach fragment compilation");
		}
		Check(gLoadCallCount == 1, "successful two-stage compilation should load blobs once");
	}

	void TestComputePassSkipsVertexStage()
	{
		ResetFakes();
		RenderPass::RenderPassDisk renderPass = MakeFullscreenPass();
		renderPass.vertexShaderSourcePath.clear();
		renderPass.vertexShaderCompiledBlobPath.clear();
		renderPass.fragmentShaderProfile = "cs_frozen";
		std::string error;

		const bool succeeded = RenderPassShaders::CompilePreparedShaders(renderPass, {}, error);

		Check(succeeded, "compute prepared compile should succeed");
		Check(gCompileRequests.size() == 1, "compute compile should submit only fragment stage");
		if (gCompileRequests.size() == 1)
			Check(gCompileRequests[0].profile == "cs_frozen", "compute profile should remain frozen");
		Check(gLoadCallCount == 1, "successful compute compilation should load blobs once");
	}

	void TestVertexFailureStopsThePipeline()
	{
		ResetFakes();
		RenderPass::RenderPassDisk renderPass = MakeFullscreenPass();
		gFailSourcePath = renderPass.vertexShaderSourcePath;
		std::string error;

		const bool succeeded = RenderPassShaders::CompilePreparedShaders(renderPass, {}, error);

		Check(!succeeded, "vertex failure should fail the prepared compile");
		Check(error == "Fullscreen vertex shader compilation failed.", "vertex failure should report its stage");
		Check(gCompileRequests.size() == 1, "vertex failure should skip fragment compile");
		Check(gLoadCallCount == 0, "vertex failure should skip blob loading");
	}

	void TestFragmentFailureStopsBlobLoading()
	{
		ResetFakes();
		RenderPass::RenderPassDisk renderPass = MakeFullscreenPass();
		gFailSourcePath = renderPass.fragmentShaderSourcePath;
		std::string error;

		const bool succeeded = RenderPassShaders::CompilePreparedShaders(renderPass, {}, error);

		Check(!succeeded, "fragment failure should fail the prepared compile");
		Check(error == "Render Pass shader compilation failed.", "fragment failure should report its stage");
		Check(gCompileRequests.size() == 2, "fragment failure should follow vertex compile");
		Check(gLoadCallCount == 0, "fragment failure should skip blob loading");
	}

	void TestLoadFailureReportsAnError()
	{
		ResetFakes();
		gLoadSucceeds = false;
		RenderPass::RenderPassDisk renderPass = MakeFullscreenPass();
		std::string error;

		const bool succeeded = RenderPassShaders::CompilePreparedShaders(renderPass, {}, error);

		Check(!succeeded, "blob load failure should fail the prepared compile");
		Check(error == "Compiled Render Pass shaders could not be loaded.", "blob load failure should report its cause");
		Check(gLoadCallCount == 1, "blob load failure should call loader once");
	}

	void TestMissingTemplateSkipsAllWork()
	{
		ResetFakes();
		gHasTemplate = false;
		RenderPass::RenderPassDisk renderPass = MakeFullscreenPass();
		std::string error;

		const bool succeeded = RenderPassShaders::CompilePreparedShaders(renderPass, {}, error);

		Check(!succeeded, "missing template should fail the prepared compile");
		Check(error == "Render Pass shader source files are missing.", "missing template should report its cause");
		Check(gCompileRequests.empty(), "missing template should skip compilation");
		Check(gLoadCallCount == 0, "missing template should skip blob loading");
	}

	void TestDownsampleUpsamplePassCompilesBothPhasesBeforeLoading()
	{
		ResetFakes();
		RenderPass::RenderPassDisk renderPass = MakeFullscreenPass();
		renderPass.type = RenderPass::RenderPassType::DownsampleUpsample;
		renderPass.executionMode = RenderPass::ExecutionMode::FullscreenPixel;
		renderPass.fragmentShaderSourcePath = "stage/downsample.hlsl";
		renderPass.fragmentShaderCompiledBlobPath = "stage/downsample-stage.blob";
		renderPass.fragmentShaderProfile = "ps_downsample";
		renderPass.fragmentShaderEntryPoint = "downMain";
		renderPass.upsampleShaderSourcePath = "stage/upsample.hlsl";
		renderPass.upsampleShaderCompiledBlobPath = "stage/upsample-stage.blob";
		renderPass.upsampleShaderProfile = "ps_upsample";
		renderPass.upsampleShaderEntryPoint = "upMain";
		const auto configuration = std::make_shared<ShaderConfiguration::CompileConfiguration>(ShaderConfiguration::DocumentDisk{});
		gExpectedConfiguration = configuration;
		std::string error;

		const bool succeeded = RenderPassShaders::CompilePreparedShaders(renderPass, {"-DCHAIN"}, error, configuration);

		Check(succeeded, "downsample/upsample prepared compile should succeed");
		Check(gCompileRequests.size() == 3, "chain compile should submit vertex, downsample, and upsample stages");
		if (gCompileRequests.size() == 3)
		{
			Check(gCompileRequests[1].sourcePath == "stage/downsample.hlsl", "downsample source should be selected independently");
			Check(gCompileRequests[1].profile == "ps_downsample" && gCompileRequests[1].entryPoint == "downMain", "downsample profile and entrypoint should remain frozen");
			Check(gCompileRequests[1].outputPath == "stage/downsample-stage.blob", "downsample blob should use its staged path");
			Check(gCompileRequests[2].sourcePath == "stage/upsample.hlsl", "upsample source should be selected independently");
			Check(gCompileRequests[2].profile == "ps_upsample" && gCompileRequests[2].entryPoint == "upMain", "upsample profile and entrypoint should remain frozen");
			Check(gCompileRequests[2].outputPath == "stage/upsample-stage.blob", "upsample blob should use its staged path");
		}
		Check(gConfigurationMatched, "both chain phases should receive the same prepared configuration snapshot");
		Check(gLoadCallCount == 1, "chain blobs should load only after both phases compile");

		ResetFakes();
		renderPass = MakeFullscreenPass();
		renderPass.type = RenderPass::RenderPassType::DownsampleUpsample;
		renderPass.executionMode = RenderPass::ExecutionMode::FullscreenPixel;
		renderPass.upsampleShaderSourcePath = "stage/upsample.hlsl";
		renderPass.upsampleShaderCompiledBlobPath = "stage/upsample-stage.blob";
		gFailSourcePath = renderPass.upsampleShaderSourcePath;
		const bool upsampleSucceeded = RenderPassShaders::CompilePreparedShaders(renderPass, {}, error);
		Check(!upsampleSucceeded, "upsample compilation failure should fail the complete chain compile");
		Check(error == "Render Pass upsample shader compilation failed.", "upsample failure should identify its phase");
		Check(gLoadCallCount == 0, "failed upsample compilation must not load or publish downsample blobs");
	}

	void TestDownsampleUpsampleTemplatesUseDistinctShaders()
	{
		for (const RenderPass::ExecutionMode executionMode : {RenderPass::ExecutionMode::FullscreenPixel, RenderPass::ExecutionMode::Compute})
		{
			ResetFakes();
			RenderPass::RenderPassDisk renderPass{};
			renderPass.id = "ChainTemplate";
			renderPass.name = "Chain Template";
			renderPass.type = RenderPass::RenderPassType::DownsampleUpsample;
			renderPass.executionMode = executionMode;
			renderPass.packageDirectory = "package";
			RenderPass::LogicalResourceBindingDisk input{};
			input.resourceId = "source";
			input.hlslName = "SourceTexture";
			input.access = RenderPass::ResourceAccess::ShaderResource;
			renderPass.inputs.push_back(input);
			RenderPass::LogicalResourceBindingDisk output{};
			output.resourceId = "destination";
			output.hlslName = "DestinationTexture";
			if (executionMode == RenderPass::ExecutionMode::Compute)
				output.access = RenderPass::ResourceAccess::UnorderedAccess;
			else
				output.access = RenderPass::ResourceAccess::RenderTarget;
			renderPass.outputs.push_back(output);
			ModifiedShader::ModifiedShaderPackageDisk modifiedShader{};
			if (executionMode == RenderPass::ExecutionMode::Compute)
				modifiedShader.shaderType = ShaderTarget::ComputeShader;
			else
				modifiedShader.shaderType = ShaderTarget::PixelShader;
			std::string error;

			const bool created = RenderPassShaders::CreateShaderTemplate(renderPass, modifiedShader, error, false);

			Check(created, "chain template creation should succeed for each execution mode");
			std::string downsampleFile = "DownsampleUpsampleDown.hlsl";
			std::string upsampleFile = "DownsampleUpsampleUp.hlsl";
			if (executionMode == RenderPass::ExecutionMode::Compute)
			{
				downsampleFile = "DownsampleUpsampleDownCS.hlsl";
				upsampleFile = "DownsampleUpsampleUpCS.hlsl";
			}
			Check(renderPass.fragmentShaderSourceFile == downsampleFile, "chain downsample should select its own source filename");
			Check(renderPass.upsampleShaderSourceFile == upsampleFile, "chain upsample should select its own source filename");
			const std::string downsamplePath = "package/" + downsampleFile;
			const std::string upsamplePath = "package/" + upsampleFile;
			Check(gWrittenFiles.find(downsamplePath) != gWrittenFiles.end(), "chain downsample source should be written");
			Check(gWrittenFiles.find(upsamplePath) != gWrittenFiles.end(), "chain upsample source should be written");
			if (gWrittenFiles.find(downsamplePath) != gWrittenFiles.end() && gWrittenFiles.find(upsamplePath) != gWrittenFiles.end())
			{
				Check(gWrittenFiles[downsamplePath].find("(c00 + c10 + c01 + c11) * 0.25") != std::string::npos,
					"downsample source should contain the reducing filter");
				Check(gWrittenFiles[upsamplePath].find("lerp(lerp(c00, c10, sourceFraction.x)") != std::string::npos,
					"upsample source should contain the interpolation filter");
			}
		}
	}
}

namespace ShaderInjectorIO
{
	bool WriteBinaryFile(const std::string& path, const void* data, size_t dataSize)
	{
		gWrittenFiles[path] = std::string(static_cast<const char*>(data), dataSize);
		return true;
	}

	bool WriteTextFileIfMissing(const std::string& path, const std::string& contents)
	{
		if (gWrittenFiles.find(path) != gWrittenFiles.end())
			return true;
		gWrittenFiles[path] = contents;
		return true;
	}

	bool FileExists(const std::string& path)
	{
		return gWrittenFiles.find(path) != gWrittenFiles.end();
	}

	bool CompileSourceToDXILBlob(
		const std::string& shaderSourceFilePath,
		const std::string& shaderProfile,
		const std::string& entryPoint,
		std::string& compiledBlobFilePath,
		ShaderSignaturePacking,
		const std::vector<std::string>&,
		const std::vector<std::string>& extraCompilerArguments,
		std::shared_ptr<const ShaderConfiguration::CompileConfiguration> configuration)
	{
		gCompileRequests.push_back({shaderSourceFilePath, shaderProfile, entryPoint, compiledBlobFilePath, extraCompilerArguments});
		if (gExpectedConfiguration && configuration != gExpectedConfiguration)
			gConfigurationMatched = false;
		return shaderSourceFilePath != gFailSourcePath;
	}
}

namespace RenderPass
{
	void ResolveShaderPaths(RenderPassDisk& renderPass)
	{
		renderPass.vertexShaderSourcePath.clear();
		renderPass.fragmentShaderSourcePath.clear();
		renderPass.upsampleShaderSourcePath.clear();
		renderPass.vertexShaderCompiledBlobPath.clear();
		renderPass.fragmentShaderCompiledBlobPath.clear();
		renderPass.upsampleShaderCompiledBlobPath.clear();
		if (!renderPass.vertexShaderSourceFile.empty())
			renderPass.vertexShaderSourcePath = "package/" + renderPass.vertexShaderSourceFile;
		if (!renderPass.fragmentShaderSourceFile.empty())
			renderPass.fragmentShaderSourcePath = "package/" + renderPass.fragmentShaderSourceFile;
		if (!renderPass.upsampleShaderSourceFile.empty())
			renderPass.upsampleShaderSourcePath = "package/" + renderPass.upsampleShaderSourceFile;
		if (!renderPass.vertexShaderCompiledBlobFile.empty())
			renderPass.vertexShaderCompiledBlobPath = "package/" + renderPass.vertexShaderCompiledBlobFile;
		if (!renderPass.fragmentShaderCompiledBlobFile.empty())
			renderPass.fragmentShaderCompiledBlobPath = "package/" + renderPass.fragmentShaderCompiledBlobFile;
		if (!renderPass.upsampleShaderCompiledBlobFile.empty())
			renderPass.upsampleShaderCompiledBlobPath = "package/" + renderPass.upsampleShaderCompiledBlobFile;
	}

	ExecutionMode ResolveExecutionMode(const RenderPassDisk& renderPass)
	{
		return renderPass.executionMode;
	}

	PassOperation ResolvePassOperation(const RenderPassDisk& renderPass)
	{
		return renderPass.operation;
	}

	bool IsReplacementPass(RenderPassType)
	{
		return false;
	}

	bool HasShaderTemplate(const RenderPassDisk&)
	{
		return gHasTemplate;
	}

	bool LoadCompiledShaderBlobs(RenderPassDisk&)
	{
		++gLoadCallCount;
		return gLoadSucceeds;
	}
}

namespace ModifiedShader
{
	bool ModifiedShaderPackageDisk::HasPerTargetDefines() const
	{
		return false;
	}
}

namespace ShaderConfiguration
{
	CompileConfiguration::CompileConfiguration(const DocumentDisk& document, std::shared_ptr<ConfigurationProgress>)
	{
		for (const PropertyDisk& property : document.properties)
			sourceFingerprints[property.sourcePath] = property.value;
	}

	std::string CompileConfiguration::GetSourceFingerprint(const std::string& sourcePath) const
	{
		const auto sourceFingerprint = sourceFingerprints.find(sourcePath);

		if (sourceFingerprint != sourceFingerprints.end())
			return sourceFingerprint->second;

		return {};
	}
}

namespace ShaderResource
{
	const char* TextureHlslTypeName(TextureDimension)
	{
		return "Texture2D";
	}
}

namespace DatabaseModifiedShaders
{
	void LoadSourceVariants(
		const ModifiedShader::ModifiedShaderPackageDisk&,
		const std::string&,
		const std::string&,
		std::vector<ModifiedShader::ModifiedShaderCompiledVariant>&,
		bool)
	{
	}

	int QueueSourceCompiles(
		const ModifiedShader::ModifiedShaderPackageDisk&,
		const std::string&,
		const std::string&,
		std::vector<ModifiedShader::ModifiedShaderCompiledVariant>&,
		std::shared_ptr<const ShaderConfiguration::CompileConfiguration>)
	{
		return 0;
	}
}

namespace DatabaseRenderPasses
{
	const ModifiedShader::ModifiedShaderPackageDisk* ResolveModifiedShader(const RenderPass::RenderPassDisk&)
	{
		return nullptr;
	}
}

namespace DatabaseShaderResources
{
	const ShaderResource::TextureDisk* FindShaderResourceById(const std::string&)
	{
		return nullptr;
	}
}

namespace DatabaseShaderConfigurations
{
	std::shared_ptr<const ShaderConfiguration::CompileConfiguration> GetAppliedConfiguration()
	{
		return {};
	}
}

namespace ShaderResourceCatalog
{
	std::vector<ShaderResource::CatalogEntry> GetSnapshot()
	{
		return {};
	}
}

namespace StringHelper
{
	std::string ShaderProfileForType(ShaderTarget::ShaderType)
	{
		return "unused-profile";
	}
}

int main()
{
	TestSelectiveStageCompilation();
	TestFullscreenPassUsesPreparedValues();
	TestComputePassSkipsVertexStage();
	TestVertexFailureStopsThePipeline();
	TestFragmentFailureStopsBlobLoading();
	TestLoadFailureReportsAnError();
	TestMissingTemplateSkipsAllWork();
	TestDownsampleUpsamplePassCompilesBothPhasesBeforeLoading();
	TestDownsampleUpsampleTemplatesUseDistinctShaders();

	if (gFailureCount != 0)
	{
		std::cerr << gFailureCount << " prepared compile check(s) failed.\n";
		return 1;
	}

	std::cout << "RenderPassPreparedCompileTests passed.\n";
	return 0;
}
