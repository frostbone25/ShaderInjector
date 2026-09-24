#include "ShaderConfiguration/ShaderConfiguration.h"

#include <utility>

#include "IO/ShaderInjectorIO.h"

namespace ShaderConfiguration
{
	bool WriteJson(const std::string& path, const DocumentDisk& document)
	{
		if (path.empty())
			return false;

		const nlohmann::ordered_json json = document;
		return ShaderInjectorIO::WriteTextFile(path, json.dump(4));
	}

	bool LoadJson(const std::string& path, DocumentDisk& outDocument)
	{
		try
		{
			std::string jsonText;

			if (!ShaderInjectorIO::ReadTextFile(path, jsonText))
				return false;

			const nlohmann::ordered_json json = nlohmann::ordered_json::parse(jsonText);
			DocumentDisk document = json.get<DocumentDisk>();

			if (document.format != formatName || document.schemaVersion != currentSchemaVersion)
			{
				return false;
			}

			outDocument = std::move(document);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}
} //namespace ShaderConfiguration