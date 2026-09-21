#include "ShaderTarget/ShaderTarget.h"
#include "ShaderTarget/ShaderTargetInternal.h"

#include <vector>

#include "IO/ShaderInjectorIO.h"

namespace ShaderTarget
{
	bool IsShaderTargetJsonFilename(const char* filename)
	{
		if (!filename)
			return false;

		const std::string name = filename;
		return name == "ShaderTarget.json";
	}

	bool WriteShaderTargetJson(const ShaderTarget::ShaderTargetDisk& replacement)
	{
		ShaderTarget::ShaderTargetDisk portableReplacement = replacement;
		Internal::MakeReplacementPortableForDisk(portableReplacement);

		nlohmann::ordered_json json = portableReplacement;
		json["shaderBytecodeHashAliases"] = portableReplacement.shaderBytecodeHashAliases;
		json["pipelineCachedBlobHashAliases"] = portableReplacement.pipelineCachedBlobHashAliases;
		json["originalShaderAnalysis"] = portableReplacement.originalShaderAnalysis;
		json["pipelineTemplates"] = portableReplacement.pipelineTemplates;
		return ShaderInjectorIO::WriteTextFile(replacement.jsonPath, json.dump(4));
	}

	bool LoadShaderTargetJson(const std::string& path, ShaderTarget::ShaderTargetDisk& outReplacement)
	{
		try
		{
			std::string jsonText;

			if (!ShaderInjectorIO::ReadTextFile(path, jsonText))
				return false;

			const nlohmann::ordered_json json = nlohmann::ordered_json::parse(jsonText);

			outReplacement = json.get<ShaderTarget::ShaderTargetDisk>();

			if (json.contains("shaderBytecodeHashAliases") && json["shaderBytecodeHashAliases"].is_array())
				outReplacement.shaderBytecodeHashAliases = json["shaderBytecodeHashAliases"].get<std::vector<std::string>>();

			if (json.contains("pipelineCachedBlobHashAliases") && json["pipelineCachedBlobHashAliases"].is_array())
				outReplacement.pipelineCachedBlobHashAliases = json["pipelineCachedBlobHashAliases"].get<std::vector<std::string>>();

			if (json.contains("originalShaderAnalysis") && json["originalShaderAnalysis"].is_object())
				outReplacement.originalShaderAnalysis = json["originalShaderAnalysis"].get<ShaderAnalysis::ShaderAnalysisDisk>();

			if (json.contains("pipelineTemplates") && json["pipelineTemplates"].is_array())
				outReplacement.pipelineTemplates = json["pipelineTemplates"].get<std::vector<ShaderTarget::ShaderPipelineTemplateDisk>>();

			Internal::ResolveReplacementPathsFromJsonLocation(outReplacement, path);

			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	void CollectShaderTargetJsonFiles(const std::string& directory, std::vector<std::string>& outJsonFiles)
	{
		std::vector<std::string> jsonFiles;
		ShaderInjectorIO::CollectFilesByExtension(directory, ShaderInjectorIO::extensionJSON, jsonFiles, true);

		for (const std::string& jsonFile : jsonFiles)
		{
			if (IsShaderTargetJsonFilename(ShaderInjectorIO::FileNameFromPath(jsonFile).c_str()))
				outJsonFiles.push_back(jsonFile);
		}
	}
}
