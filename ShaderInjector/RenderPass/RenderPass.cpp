#include "RenderPass/RenderPass.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include "Hash/Hash.h"
#include "IO/ShaderInjectorIO.h"

namespace RenderPass
{
	ExecutionMode ResolveExecutionMode(const RenderPassDisk& renderPass)
	{
		if (renderPass.executionMode != ExecutionMode::Automatic)
			return renderPass.executionMode;

		if (renderPass.type == RenderPassType::ReplacementComputeShader)
			return ExecutionMode::Compute;
		return ExecutionMode::FullscreenPixel;
	}

	PassOperation ResolvePassOperation(const RenderPassDisk& renderPass)
	{
		if (renderPass.operation != PassOperation::Automatic)
			return renderPass.operation;

		switch (renderPass.type)
		{
		case RenderPassType::MipChain:
			return PassOperation::MipChain;
		case RenderPassType::TemporalHistory:
			return PassOperation::TemporalHistory;
		case RenderPassType::ReplacementPixelShader:
		case RenderPassType::ReplacementComputeShader:
			return PassOperation::ReplaceOriginal;
		case RenderPassType::Custom:
		default:
			return PassOperation::Custom;
		}
	}

	const LogicalResourceBindingDisk* FindMipChainRuntimeSource(const RenderPassDisk& renderPass)
	{
		if (ResolvePassOperation(renderPass) != PassOperation::MipChain)
			return nullptr;

		for (const LogicalResourceBindingDisk& input : renderPass.inputs)
		{
			if (input.origin == ShaderResource::ResourceOrigin::Runtime &&
				input.access == ResourceAccess::ShaderResource)
				return &input;
		}

		return nullptr;
	}

	std::string MipChainOutputResourceId(const RenderPassDisk& renderPass)
	{
		return renderPass.id + ":MipChain";
	}

	std::string TemporalHistoryResourceId(const RenderPassDisk& renderPass)
	{
		return renderPass.id + ":History";
	}

	void ConfigureTemporalHistoryPass(RenderPassDisk& renderPass)
	{
		if (renderPass.type != RenderPassType::TemporalHistory)
			return;

		renderPass.operation = PassOperation::Automatic;
		const bool createdSource = renderPass.inputs.empty();

		if (createdSource)
			renderPass.inputs.emplace_back();

		renderPass.inputs.resize(1);
		LogicalResourceBindingDisk& source = renderPass.inputs.front();
		source.access = ResourceAccess::CopySource;

		if (createdSource || source.origin == ShaderResource::ResourceOrigin::Disk)
			source.origin = ShaderResource::ResourceOrigin::Game;

		const std::string historyResourceId = TemporalHistoryResourceId(renderPass);
		auto definitionIt = std::find_if(
			renderPass.runtimeResources.begin(),
			renderPass.runtimeResources.end(),
			[&](const RuntimeResourceDefinitionDisk& definition)
			{
				return definition.id == historyResourceId;
			});

		if (definitionIt == renderPass.runtimeResources.end())
		{
			RuntimeResourceDefinitionDisk definition{};
			definition.id = historyResourceId;
			renderPass.runtimeResources.push_back(std::move(definition));
			definitionIt = std::prev(renderPass.runtimeResources.end());
		}

		definitionIt->name = "Temporal History";
		if (!renderPass.name.empty())
			definitionIt->name = renderPass.name + " History";

		definitionIt->texture.matchReferenceTexture = true;
		definitionIt->texture.lifetime = ShaderResource::ResourceLifetime::History;
		definitionIt->texture.allowRenderTarget = false;
		definitionIt->texture.allowUnorderedAccess = false;

		renderPass.outputs.resize(1);
		LogicalResourceBindingDisk& destination = renderPass.outputs.front();
		destination.resourceId = historyResourceId;

		if (destination.hlslName.empty())
			destination.hlslName = "SI_TemporalHistory";

		destination.origin = ShaderResource::ResourceOrigin::Runtime;
		destination.access = ResourceAccess::CopyDestination;
		destination.temporalView = ShaderResource::TemporalView::Current;
	}

	void NormalizeExecutionResources(RenderPassDisk& renderPass)
	{
		ConfigureTemporalHistoryPass(renderPass);

		if (renderPass.type != RenderPassType::Custom &&
			renderPass.type != RenderPassType::TemporalHistory)
			return;

		const PassOperation operation = ResolvePassOperation(renderPass);

		if (operation == PassOperation::Copy || operation == PassOperation::TemporalHistory)
		{
			for (LogicalResourceBindingDisk& input : renderPass.inputs)
			{
				if (input.origin != ShaderResource::ResourceOrigin::Disk)
					input.access = ResourceAccess::CopySource;
			}

			for (LogicalResourceBindingDisk& output : renderPass.outputs)
			{
				if (output.origin != ShaderResource::ResourceOrigin::Runtime || output.resourceId.empty())
				{
					continue;
				}

				output.access = ResourceAccess::CopyDestination;

				const auto definitionIt = std::find_if(
					renderPass.runtimeResources.begin(),
					renderPass.runtimeResources.end(),
					[&](const RuntimeResourceDefinitionDisk& definition)
					{
						return definition.id == output.resourceId;
					});

				if (definitionIt != renderPass.runtimeResources.end())
				{
					definitionIt->texture.matchReferenceTexture = true;
					definitionIt->texture.allowRenderTarget = false;
					definitionIt->texture.allowUnorderedAccess = false;
				}
			}

			return;
		}

		if (operation != PassOperation::Custom &&
			operation != PassOperation::Downsample &&
			operation != PassOperation::UpsampleChain)
			return;

		const ExecutionMode executionMode = ResolveExecutionMode(renderPass);

		if (executionMode != ExecutionMode::Compute &&
			executionMode != ExecutionMode::FullscreenPixel)
		{
			return;
		}

		for (LogicalResourceBindingDisk& output : renderPass.outputs)
		{
			if (output.origin != ShaderResource::ResourceOrigin::Runtime || output.resourceId.empty())
				continue;

			if (executionMode == ExecutionMode::Compute && output.access == ResourceAccess::RenderTarget)
				output.access = ResourceAccess::UnorderedAccess;
			else if (executionMode == ExecutionMode::FullscreenPixel && output.access == ResourceAccess::UnorderedAccess)
				output.access = ResourceAccess::RenderTarget;

			const auto definitionIt = std::find_if(
				renderPass.runtimeResources.begin(),
				renderPass.runtimeResources.end(),
				[&](const RuntimeResourceDefinitionDisk& definition)
				{
					return definition.id == output.resourceId;
				});

			if (definitionIt == renderPass.runtimeResources.end())
				continue;

			if (output.access == ResourceAccess::UnorderedAccess)
				definitionIt->texture.allowUnorderedAccess = true;
			else if (output.access == ResourceAccess::RenderTarget)
				definitionIt->texture.allowRenderTarget = true;
		}

		if (executionMode == ExecutionMode::Compute &&
			(operation == PassOperation::Downsample || operation == PassOperation::UpsampleChain))
		{
			renderPass.dispatch.mode = DispatchMode::ScaleByResolution;
		}
	}

	bool IsTimingValid(const std::string& timing)
	{
		return timing == timingBefore || timing == timingAfter;
	}

	bool WriteJson(const RenderPassDisk& renderPass)
	{
		if (renderPass.jsonPath.empty() || renderPass.id.empty())
			return false;

		RenderPassDisk portableRenderPass = renderPass;
		NormalizeExecutionResources(portableRenderPass);
		portableRenderPass.packageDirectory.clear();
		portableRenderPass.jsonPath.clear();
		portableRenderPass.vertexShaderSourcePath.clear();
		portableRenderPass.fragmentShaderSourcePath.clear();
		portableRenderPass.vertexShaderCompiledBlobPath.clear();
		portableRenderPass.fragmentShaderCompiledBlobPath.clear();
		portableRenderPass.vertexShaderBlob.clear();
		portableRenderPass.fragmentShaderBlob.clear();
		portableRenderPass.vertexShaderBlobHash = 0;
		portableRenderPass.fragmentShaderBlobHash = 0;
		nlohmann::ordered_json json = portableRenderPass;
		return ShaderInjectorIO::WriteTextFile(renderPass.jsonPath, json.dump(4));
	}

	bool LoadJson(const std::string& jsonPath, RenderPassDisk& outRenderPass)
	{
		try
		{
			std::string jsonText;

			if (!ShaderInjectorIO::ReadTextFile(jsonPath, jsonText))
				return false;

			nlohmann::ordered_json json = nlohmann::ordered_json::parse(jsonText);

			if (!json.contains("event"))
			{
				EventReferenceDisk legacyEvent{};
				legacyEvent.type = EventType::ModifiedShader;
				legacyEvent.id = json.value("modifiedShaderId", std::string());
				json["event"] = legacyEvent;
			}

			// existing packages stored imported textures separately; load them as inputs
			// before decoding so their original registers survive the format change.
			if (json.contains("shaderResources") && json["shaderResources"].is_array())
			{
				if (!json.contains("inputs") || !json["inputs"].is_array())
					json["inputs"] = nlohmann::ordered_json::array();
				for (const auto& resource : json["shaderResources"])
				{
					LogicalResourceBindingDisk input{};
					input.resourceId = resource.value("resourceId", std::string());
					input.hlslName = resource.value("hlslName", std::string());
					input.origin = ShaderResource::ResourceOrigin::Disk;
					input.access = ResourceAccess::ShaderResource;
					input.shaderRegister = resource.value("shaderRegister", 0u);
					input.registerSpace = resource.value("registerSpace", 0u);
					const bool alreadyPresent = std::any_of(json["inputs"].begin(), json["inputs"].end(), [&](const auto& existing)
					{
						return existing.value("origin", std::string()) == "Disk" &&
							existing.value("resourceId", std::string()) == input.resourceId &&
							existing.value("shaderRegister", 0u) == input.shaderRegister &&
							existing.value("registerSpace", 0u) == input.registerSpace;
					});
					if (!alreadyPresent)
						json["inputs"].push_back(input);
				}
			}

			RenderPassDisk renderPass = json.get<RenderPassDisk>();

			if (renderPass.format != formatName || renderPass.id.empty())
				return false;

			if (renderPass.name.empty())
				renderPass.name = renderPass.id;

			if (!IsTimingValid(renderPass.timing))
				renderPass.timing = timingBefore;

			if (!ShaderResource::IsValidDownscaleFactor(renderPass.resolution.downscaleFactor))
				renderPass.resolution.downscaleFactor = 1;

			if (!renderPass.dispatch.threadGroupSizeX)
				renderPass.dispatch.threadGroupSizeX = 8;

			if (!renderPass.dispatch.threadGroupSizeY)
				renderPass.dispatch.threadGroupSizeY = 8;

			if (!renderPass.dispatch.threadGroupSizeZ)
				renderPass.dispatch.threadGroupSizeZ = 1;

			if (renderPass.type == RenderPassType::MipChain &&
				!FindMipChainRuntimeSource(renderPass) &&
				renderPass.event.type == EventType::ModifiedShader)
			{
				renderPass.timing = timingBefore;
			}

			if (IsReplacementPass(renderPass.type))
				renderPass.timing = timingBefore;

			NormalizeExecutionResources(renderPass);
			renderPass.schemaVersion = currentSchemaVersion;

			renderPass.packageDirectory = ShaderInjectorIO::DirectoryFromPath(jsonPath);
			renderPass.jsonPath = jsonPath;
			ResolveShaderPaths(renderPass);
			LoadCompiledShaderBlobs(renderPass);
			outRenderPass = std::move(renderPass);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	void ResolveShaderPaths(RenderPassDisk& renderPass)
	{
		//resolve only the shader files named by the portable pass configuration.
		renderPass.vertexShaderSourcePath.clear();
		renderPass.fragmentShaderSourcePath.clear();
		renderPass.vertexShaderCompiledBlobPath.clear();
		renderPass.fragmentShaderCompiledBlobPath.clear();
		if (!renderPass.vertexShaderSourceFile.empty())
			renderPass.vertexShaderSourcePath = ShaderInjectorIO::JoinPath(renderPass.packageDirectory, renderPass.vertexShaderSourceFile);
		if (!renderPass.fragmentShaderSourceFile.empty())
			renderPass.fragmentShaderSourcePath = ShaderInjectorIO::JoinPath(renderPass.packageDirectory, renderPass.fragmentShaderSourceFile);
		if (!renderPass.vertexShaderCompiledBlobFile.empty())
			renderPass.vertexShaderCompiledBlobPath = ShaderInjectorIO::JoinPath(renderPass.packageDirectory, renderPass.vertexShaderCompiledBlobFile);
		if (!renderPass.fragmentShaderCompiledBlobFile.empty())
			renderPass.fragmentShaderCompiledBlobPath = ShaderInjectorIO::JoinPath(renderPass.packageDirectory, renderPass.fragmentShaderCompiledBlobFile);
	}

	bool LoadCompiledShaderBlobs(RenderPassDisk& renderPass)
	{
		renderPass.vertexShaderBlob.clear();
		renderPass.fragmentShaderBlob.clear();
		renderPass.vertexShaderBlobHash = 0;
		renderPass.fragmentShaderBlobHash = 0;
		const bool computePass = ResolveExecutionMode(renderPass) == ExecutionMode::Compute;
		const bool fragmentOnlyPass = IsReplacementPass(renderPass.type) || computePass;

		if ((!fragmentOnlyPass && renderPass.vertexShaderCompiledBlobPath.empty()) ||
			renderPass.fragmentShaderCompiledBlobPath.empty())
		{
			return false;
		}

		const bool vertexLoaded = fragmentOnlyPass || ShaderInjectorIO::LoadDXILBlobFromDisk(renderPass.vertexShaderCompiledBlobPath, renderPass.vertexShaderBlob);
		const bool fragmentLoaded = ShaderInjectorIO::LoadDXILBlobFromDisk(renderPass.fragmentShaderCompiledBlobPath, renderPass.fragmentShaderBlob);

		if (!vertexLoaded || !fragmentLoaded || !HasCompiledShaders(renderPass))
			return false;

		renderPass.vertexShaderBlobHash = 0;
		if (!renderPass.vertexShaderBlob.empty())
			renderPass.vertexShaderBlobHash = Hash::HashMemory(renderPass.vertexShaderBlob.data(), renderPass.vertexShaderBlob.size());
		renderPass.fragmentShaderBlobHash = Hash::HashMemory(renderPass.fragmentShaderBlob.data(), renderPass.fragmentShaderBlob.size());

		return true;
	}

	bool HasShaderTemplate(const RenderPassDisk& renderPass)
	{
		if (IsReplacementPass(renderPass.type) ||
			ResolveExecutionMode(renderPass) == ExecutionMode::Compute)
		{
			return !renderPass.fragmentShaderSourcePath.empty() && ShaderInjectorIO::FileExists(renderPass.fragmentShaderSourcePath);
		}

		return !renderPass.vertexShaderSourcePath.empty() &&
			   !renderPass.fragmentShaderSourcePath.empty() &&
			   ShaderInjectorIO::FileExists(renderPass.vertexShaderSourcePath) &&
			   ShaderInjectorIO::FileExists(renderPass.fragmentShaderSourcePath);
	}

	bool HasCompiledShaders(const RenderPassDisk& renderPass)
	{
		if (IsReplacementPass(renderPass.type) ||
			ResolveExecutionMode(renderPass) == ExecutionMode::Compute)
			return !renderPass.fragmentShaderBlob.empty();

		return !renderPass.vertexShaderBlob.empty() && !renderPass.fragmentShaderBlob.empty();
	}

	bool IsReplacementPass(RenderPassType type)
	{
		return type == RenderPassType::ReplacementPixelShader ||
			   type == RenderPassType::ReplacementComputeShader;
	}
} //namespace RenderPass
