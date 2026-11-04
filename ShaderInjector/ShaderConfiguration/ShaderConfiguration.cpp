#include "ShaderConfiguration/ShaderConfiguration.h"

#include <utility>
#include <cctype>

#include "IO/ShaderInjectorIO.h"

namespace ShaderConfiguration
{
	std::string GetPresetFileName(const std::string& presetID)
	{
		if (presetID.empty())
			return {};

		for (const unsigned char character : presetID)
		{
			if (!(std::isalnum(character) || character == '_' || character == '-'))
				return {};
		}

		return presetID + ".json";
	}

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

			if (document.format != formatName || document.schemaVersion < 1 || document.schemaVersion > currentSchemaVersion)
			{
				return false;
			}

			for (PropertyDisk& property : document.properties)
			{
				if (property.value.empty())
					property.value = property.defaultValue;
			}

			document.schemaVersion = currentSchemaVersion;

			outDocument = std::move(document);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool WritePresetJson(const std::string& path, const PresetDisk& preset)
	{
		if (path.empty() || GetPresetFileName(preset.id).empty())
			return false;

		const nlohmann::ordered_json json = preset;
		return ShaderInjectorIO::WriteTextFile(path, json.dump(4));
	}

	bool WritePresetAtomically(const std::string& path, const PresetDisk& preset, std::string& outError)
	{
		outError.clear();
		const std::string temporaryPath = path + ".pending";
		const std::string backupPath = path + ".backup";
		ShaderInjectorIO::DirectoryCreate(ShaderInjectorIO::DirectoryFromPath(path));
		ShaderInjectorIO::DeleteFileIfExists(temporaryPath);
		ShaderInjectorIO::DeleteFileIfExists(backupPath);

		if (!WritePresetJson(temporaryPath, preset))
		{
			outError = "Could not stage the preset file.";
			return false;
		}

		const bool hadPreset = ShaderInjectorIO::FileExists(path);
		if (hadPreset && !ShaderInjectorIO::MovePath(path, backupPath))
		{
			ShaderInjectorIO::DeleteFileIfExists(temporaryPath);
			outError = "Could not stage the existing preset file.";
			return false;
		}

		if (!ShaderInjectorIO::MovePath(temporaryPath, path))
		{
			if (hadPreset)
				ShaderInjectorIO::MovePath(backupPath, path, true);
			ShaderInjectorIO::DeleteFileIfExists(temporaryPath);
			outError = "Could not publish the preset file.";
			return false;
		}

		ShaderInjectorIO::DeleteFileIfExists(backupPath);
		return true;
	}

	bool LoadPresetJson(const std::string& path, PresetDisk& outPreset)
	{
		try
		{
			std::string jsonText;
			if (!ShaderInjectorIO::ReadTextFile(path, jsonText))
				return false;

			PresetDisk preset = nlohmann::ordered_json::parse(jsonText).get<PresetDisk>();
			if (GetPresetFileName(preset.id).empty())
				return false;

			outPreset = std::move(preset);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool WriteCatalogAndPreset(
		const std::string& catalogPath,
		const std::string& presetPath,
		const DocumentDisk& document,
		const PresetDisk& preset,
		std::string& outError)
	{
		outError.clear();
		const std::string catalogTemporaryPath = catalogPath + ".pending";
		const std::string presetTemporaryPath = presetPath + ".pending";
		const std::string catalogBackupPath = catalogPath + ".backup";
		const std::string presetBackupPath = presetPath + ".backup";
		const std::string presetFileName = GetPresetFileName(preset.id);

		if (catalogPath.empty() || presetPath.empty() || presetFileName.empty())
		{
			outError = "Preset or catalog path is invalid.";
			return false;
		}

		ShaderInjectorIO::DirectoryCreate(ShaderInjectorIO::DirectoryFromPath(catalogPath));
		ShaderInjectorIO::DirectoryCreate(ShaderInjectorIO::DirectoryFromPath(presetPath));
		ShaderInjectorIO::DeleteFileIfExists(catalogTemporaryPath);
		ShaderInjectorIO::DeleteFileIfExists(presetTemporaryPath);

		if (!WriteJson(catalogTemporaryPath, document) || !WritePresetJson(presetTemporaryPath, preset))
		{
			ShaderInjectorIO::DeleteFileIfExists(catalogTemporaryPath);
			ShaderInjectorIO::DeleteFileIfExists(presetTemporaryPath);
			outError = "Could not stage preset and catalog files.";
			return false;
		}

		const bool hadCatalog = ShaderInjectorIO::FileExists(catalogPath);
		const bool hadPreset = ShaderInjectorIO::FileExists(presetPath);
		ShaderInjectorIO::DeleteFileIfExists(catalogBackupPath);
		ShaderInjectorIO::DeleteFileIfExists(presetBackupPath);

		if (hadCatalog && !ShaderInjectorIO::MovePath(catalogPath, catalogBackupPath))
		{
			ShaderInjectorIO::DeleteFileIfExists(catalogTemporaryPath);
			ShaderInjectorIO::DeleteFileIfExists(presetTemporaryPath);
			outError = "Could not stage the existing configuration catalog.";
			return false;
		}

		if (!ShaderInjectorIO::MovePath(catalogTemporaryPath, catalogPath))
		{
			if (hadCatalog)
				ShaderInjectorIO::MovePath(catalogBackupPath, catalogPath, true);
			ShaderInjectorIO::DeleteFileIfExists(catalogTemporaryPath);
			ShaderInjectorIO::DeleteFileIfExists(presetTemporaryPath);
			outError = "Could not publish the configuration catalog.";
			return false;
		}

		if (hadPreset && !ShaderInjectorIO::MovePath(presetPath, presetBackupPath))
		{
			ShaderInjectorIO::DeleteFileIfExists(catalogPath);
			if (hadCatalog)
				ShaderInjectorIO::MovePath(catalogBackupPath, catalogPath, true);
			ShaderInjectorIO::DeleteFileIfExists(presetTemporaryPath);
			outError = "Could not stage the existing preset.";
			return false;
		}

		if (!ShaderInjectorIO::MovePath(presetTemporaryPath, presetPath))
		{
			ShaderInjectorIO::DeleteFileIfExists(catalogPath);
			if (hadCatalog)
				ShaderInjectorIO::MovePath(catalogBackupPath, catalogPath, true);
			if (hadPreset)
				ShaderInjectorIO::MovePath(presetBackupPath, presetPath, true);
			outError = "Could not publish the preset.";
			return false;
		}

		ShaderInjectorIO::DeleteFileIfExists(catalogBackupPath);
		ShaderInjectorIO::DeleteFileIfExists(presetBackupPath);
		return true;
	}
} //namespace ShaderConfiguration
