#include "RenderPass/RenderPass.h"

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
			case RenderPassType::ReplacementPixelShader: return "Replacement Pixel Shader";
			case RenderPassType::ReplacementComputeShader: return "Replacement Compute Shader";
			case RenderPassType::Custom:
			default: return "Custom";
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
			if (renderPass.type == RenderPassType::MipChain &&
				renderPass.event.type == EventType::ModifiedShader)
			{
				renderPass.timing = timingBefore;
			}
			if (IsReplacementPass(renderPass.type))
				renderPass.timing = timingBefore;
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
		const bool replacementPass = IsReplacementPass(renderPass.type);
		if ((!replacementPass && renderPass.vertexShaderCompiledBlobPath.empty()) ||
			renderPass.fragmentShaderCompiledBlobPath.empty())
		{
			return false;
		}

		const bool vertexLoaded = replacementPass || ShaderInjectorIO::LoadDXILBlobFromDisk(
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
		if (renderPass.type == RenderPassType::ReplacementComputeShader)
		{
			return !renderPass.fragmentShaderSourcePath.empty() &&
				ShaderInjectorIO::FileExists(renderPass.fragmentShaderSourcePath);
		}
		if (renderPass.type == RenderPassType::ReplacementPixelShader)
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
		if (IsReplacementPass(renderPass.type))
			return !renderPass.fragmentShaderBlob.empty();
		return !renderPass.vertexShaderBlob.empty() && !renderPass.fragmentShaderBlob.empty();
	}

	bool IsReplacementPass(RenderPassType type)
	{
		return type == RenderPassType::ReplacementPixelShader ||
			type == RenderPassType::ReplacementComputeShader;
	}
}
