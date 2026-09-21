#include "ShaderTarget/ShaderTarget.h"

#include <string>

#include "IO/ShaderInjectorIO.h"

namespace ShaderTarget
{
	bool WritePipelineStreamMetadataJson(const std::string& path, const ShaderTarget::ShaderPipelineStreamMetadataDisk& metadata)
	{
		nlohmann::ordered_json json = metadata;
		return ShaderInjectorIO::WriteTextFile(path, json.dump(4));
	}

	bool LoadPipelineStreamMetadataJson(const std::string& path, ShaderTarget::ShaderPipelineStreamMetadataDisk& outMetadata)
	{
		try
		{
			std::string jsonText;

			if (!ShaderInjectorIO::ReadTextFile(path, jsonText))
				return false;

			const nlohmann::ordered_json json = nlohmann::ordered_json::parse(jsonText);
			outMetadata = json.get<ShaderTarget::ShaderPipelineStreamMetadataDisk>();
			return true;
		}
		catch (...)
		{
			return false;
		}
	}
}
