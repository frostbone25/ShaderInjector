#include "RenderPass.h"

#include <utility>

#include "Hash.h"
#include "ShaderInjectorIO.h"

namespace RenderPass
{
	const char* TypeName(RenderPassType type)
	{
		switch (type)
		{
			case RenderPassType::MipChain: return "MipChain";
			case RenderPassType::Custom:
			default: return "Custom";
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

			const nlohmann::ordered_json json = nlohmann::ordered_json::parse(jsonText);
			RenderPassDisk renderPass = json.get<RenderPassDisk>();
			if (renderPass.format != formatName || renderPass.id.empty())
				return false;

			if (renderPass.name.empty())
				renderPass.name = renderPass.id;

			if (!IsTimingValid(renderPass.timing))
				renderPass.timing = timingBefore;
			if (renderPass.type == RenderPassType::MipChain)
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
		if (renderPass.vertexShaderCompiledBlobPath.empty() ||
			renderPass.fragmentShaderCompiledBlobPath.empty())
		{
			return false;
		}

		const bool vertexLoaded = ShaderInjectorIO::LoadDXILBlobFromDisk(
			renderPass.vertexShaderCompiledBlobPath,
			renderPass.vertexShaderBlob);
		const bool fragmentLoaded = ShaderInjectorIO::LoadDXILBlobFromDisk(
			renderPass.fragmentShaderCompiledBlobPath,
			renderPass.fragmentShaderBlob);
		if (!vertexLoaded || !fragmentLoaded || !HasCompiledShaders(renderPass))
			return false;

		renderPass.vertexShaderBlobHash = Hash::HashMemory(
			renderPass.vertexShaderBlob.data(),
			renderPass.vertexShaderBlob.size());
		renderPass.fragmentShaderBlobHash = Hash::HashMemory(
			renderPass.fragmentShaderBlob.data(),
			renderPass.fragmentShaderBlob.size());
		return true;
	}

	bool HasShaderTemplate(const RenderPassDisk& renderPass)
	{
		return !renderPass.vertexShaderSourcePath.empty() &&
			!renderPass.fragmentShaderSourcePath.empty() &&
			ShaderInjectorIO::FileExists(renderPass.vertexShaderSourcePath) &&
			ShaderInjectorIO::FileExists(renderPass.fragmentShaderSourcePath);
	}

	bool HasCompiledShaders(const RenderPassDisk& renderPass)
	{
		return !renderPass.vertexShaderBlob.empty() && !renderPass.fragmentShaderBlob.empty();
	}
}
