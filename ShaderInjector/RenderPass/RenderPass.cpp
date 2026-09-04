#include "RenderPass/RenderPass.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include "Hash.h"
#include "IO/ShaderInjectorIO.h"

namespace RenderPass
{
	const char* TypeName(RenderPassType type)
	{
		switch (type)
		{
			case RenderPassType::MipChain: return "MipChain";
			case RenderPassType::TemporalHistory: return "Temporal History";
			case RenderPassType::ReplacementPixelShader: return "Replacement Pixel Shader";
			case RenderPassType::ReplacementComputeShader: return "Replacement Compute Shader";
			case RenderPassType::Custom:
			default: return "Custom";
		}
	}

	const char* ExecutionModeName(ExecutionMode mode)
	{
		switch (mode)
		{
			case ExecutionMode::FullscreenPixel: return "Fullscreen Pixel";
			case ExecutionMode::Compute: return "Compute";
			case ExecutionMode::Automatic:
			default: return "Automatic";
		}
	}

	const char* PassOperationName(PassOperation operation)
	{
		switch (operation)
		{
			case PassOperation::Custom: return "Custom";
			case PassOperation::ReplaceOriginal: return "Replace Original";
			case PassOperation::MipChain: return "Mip Chain";
			case PassOperation::Downsample: return "Downsample";
			case PassOperation::UpsampleChain: return "Upsample Chain";
			case PassOperation::Copy: return "Copy";
			case PassOperation::TemporalHistory: return "Temporal History Copy";
			case PassOperation::Resolve: return "Resolve";
			case PassOperation::Automatic:
			default: return "Automatic";
		}
	}

	ExecutionMode ResolveExecutionMode(const RenderPassDisk& renderPass)
	{
		if (renderPass.executionMode != ExecutionMode::Automatic)
			return renderPass.executionMode;
		return renderPass.type == RenderPassType::ReplacementComputeShader
			? ExecutionMode::Compute
			: ExecutionMode::FullscreenPixel;
	}

	PassOperation ResolvePassOperation(const RenderPassDisk& renderPass)
	{
		if (renderPass.operation != PassOperation::Automatic)
			return renderPass.operation;
		switch (renderPass.type)
		{
			case RenderPassType::MipChain: return PassOperation::MipChain;
			case RenderPassType::TemporalHistory: return PassOperation::TemporalHistory;
			case RenderPassType::ReplacementPixelShader:
			case RenderPassType::ReplacementComputeShader: return PassOperation::ReplaceOriginal;
			case RenderPassType::Custom:
			default: return PassOperation::Custom;
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
		definitionIt->name = renderPass.name.empty()
			? "Temporal History"
			: renderPass.name + " History";
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
				input.access = ResourceAccess::CopySource;

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

	const char* EventTypeName(EventType type)
	{
		switch (type)
		{
			case EventType::RenderPass: return "Render Pass";
			case EventType::ModifiedShader:
			default: return "Modified Shader";
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
		renderPass.vertexShaderSourcePath = renderPass.vertexShaderSourceFile.empty()
			? std::string()
			: ShaderInjectorIO::JoinPath(renderPass.packageDirectory, renderPass.vertexShaderSourceFile);
		renderPass.fragmentShaderSourcePath = renderPass.fragmentShaderSourceFile.empty()
			? std::string()
			: ShaderInjectorIO::JoinPath(renderPass.packageDirectory, renderPass.fragmentShaderSourceFile);
		renderPass.vertexShaderCompiledBlobPath = renderPass.vertexShaderCompiledBlobFile.empty()
			? std::string()
			: ShaderInjectorIO::JoinPath(renderPass.packageDirectory, renderPass.vertexShaderCompiledBlobFile);
		renderPass.fragmentShaderCompiledBlobPath = renderPass.fragmentShaderCompiledBlobFile.empty()
			? std::string()
			: ShaderInjectorIO::JoinPath(renderPass.packageDirectory, renderPass.fragmentShaderCompiledBlobFile);
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

		const bool vertexLoaded = fragmentOnlyPass || ShaderInjectorIO::LoadDXILBlobFromDisk(
			renderPass.vertexShaderCompiledBlobPath,
			renderPass.vertexShaderBlob);
		const bool fragmentLoaded = ShaderInjectorIO::LoadDXILBlobFromDisk(
			renderPass.fragmentShaderCompiledBlobPath,
			renderPass.fragmentShaderBlob);
		if (!vertexLoaded || !fragmentLoaded || !HasCompiledShaders(renderPass))
			return false;

		renderPass.vertexShaderBlobHash = renderPass.vertexShaderBlob.empty() ? 0 : Hash::HashMemory(
			renderPass.vertexShaderBlob.data(),
			renderPass.vertexShaderBlob.size());
		renderPass.fragmentShaderBlobHash = Hash::HashMemory(
			renderPass.fragmentShaderBlob.data(),
			renderPass.fragmentShaderBlob.size());
		return true;
	}

	bool HasShaderTemplate(const RenderPassDisk& renderPass)
	{
		if (IsReplacementPass(renderPass.type) ||
			ResolveExecutionMode(renderPass) == ExecutionMode::Compute)
		{
			return !renderPass.fragmentShaderSourcePath.empty() &&
				ShaderInjectorIO::FileExists(renderPass.fragmentShaderSourcePath);
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
}
