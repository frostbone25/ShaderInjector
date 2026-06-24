#include "ShaderReplacement.h"

#include <fstream>
#include <vector>

#include <windows.h>

#include "ShaderInjectorIO.h"

namespace ShaderReplacement
{
	std::string DirectoryFromPath(const std::string& path)
	{
		const size_t slash = path.find_last_of("\\/");
		return slash == std::string::npos ? "" : path.substr(0, slash);
	}

	std::string FileNameFromPath(const std::string& path)
	{
		const size_t slash = path.find_last_of("\\/");
		return slash == std::string::npos ? path : path.substr(slash + 1);
	}

	bool IsAbsolutePath(const std::string& path)
	{
		if (path.size() >= 3 && path[1] == ':' && (path[2] == '\\' || path[2] == '/'))
			return true;
		return path.size() >= 2 && ((path[0] == '\\' && path[1] == '\\') || (path[0] == '/' && path[1] == '/'));
	}

	void MakePathFieldPortable(std::string& path)
	{
		if (!path.empty())
			path = FileNameFromPath(path);
	}

	void ResolvePathField(std::string& path, const std::string& replacementDirectory)
	{
		if (path.empty() || replacementDirectory.empty())
			return;

		path = replacementDirectory + "\\" + FileNameFromPath(path);
	}

	std::string ShaderSourceSubdirectoryForType(ShaderReplacement::ShaderType shaderType)
	{
		return ShaderReplacement::ShaderTypeToString(shaderType) + "s";
	}

	std::vector<std::string*> PathFields(ShaderReplacement::ShaderReplacementDisk& replacement)
	{
		std::vector<std::string*> paths =
		{
			&replacement.originalShaderBlobPath,
			&replacement.modifiedShaderBlobPath,
			&replacement.jsonPath,
			&replacement.pipelineCachedBlobPath,
			&replacement.pipelineStreamBlobPath,
			&replacement.pipelineStreamMetadataPath,
			&replacement.rootSignatureBlobPath,
			&replacement.vertexShaderBlobPath,
			&replacement.pixelShaderBlobPath,
			&replacement.computeShaderBlobPath,
			&replacement.geometryShaderBlobPath,
			&replacement.hullShaderBlobPath,
			&replacement.domainShaderBlobPath,
		};

		for (ShaderReplacement::ShaderPipelineTemplateDisk& pipelineTemplate : replacement.pipelineTemplates)
		{
			paths.push_back(&pipelineTemplate.pipelineCachedBlobPath);
			paths.push_back(&pipelineTemplate.pipelineStreamBlobPath);
			paths.push_back(&pipelineTemplate.pipelineStreamMetadataPath);
			paths.push_back(&pipelineTemplate.rootSignatureBlobPath);
			paths.push_back(&pipelineTemplate.vertexShaderBlobPath);
			paths.push_back(&pipelineTemplate.pixelShaderBlobPath);
			paths.push_back(&pipelineTemplate.computeShaderBlobPath);
			paths.push_back(&pipelineTemplate.geometryShaderBlobPath);
			paths.push_back(&pipelineTemplate.hullShaderBlobPath);
			paths.push_back(&pipelineTemplate.domainShaderBlobPath);
		}

		return paths;
	}

	void MakeReplacementPortableForDisk(ShaderReplacement::ShaderReplacementDisk& replacement)
	{
		if (replacement.shaderSourceName.empty() && !replacement.shaderSourcePath.empty())
			replacement.shaderSourceName = FileNameFromPath(replacement.shaderSourcePath);

		if (!replacement.shaderSourceName.empty())
			replacement.shaderSourcePath = replacement.shaderSourceName;

		for (std::string* path : PathFields(replacement))
			MakePathFieldPortable(*path);

		replacement.replacementDirectory = ".";
	}

	void ResolveReplacementPathsFromJsonLocation(ShaderReplacement::ShaderReplacementDisk& replacement, const std::string& jsonPath)
	{
		const std::string replacementDirectory = DirectoryFromPath(jsonPath);
		replacement.replacementDirectory = replacementDirectory;

		for (std::string* path : PathFields(replacement))
			ResolvePathField(*path, replacementDirectory);

		const std::string legacySourcePath = replacement.shaderSourcePath.empty()
			? ""
			: replacementDirectory + "\\" + FileNameFromPath(replacement.shaderSourcePath);

		if (replacement.shaderSourceName.empty() && !replacement.shaderSourcePath.empty())
			replacement.shaderSourceName = FileNameFromPath(replacement.shaderSourcePath);

		if (!replacement.shaderSourceName.empty())
		{
			const std::string centralSourceDirectory = ShaderInjectorIO::GetShaderSourcesDirectory(ShaderSourceSubdirectoryForType(replacement.shaderType));
			const std::string centralSourcePath =
				centralSourceDirectory +
				"\\" +
				replacement.shaderSourceName;

			ShaderInjectorIO::DirectoryCreate(ShaderInjectorIO::GetShaderSourcesDirectory());
			ShaderInjectorIO::DirectoryCreate(centralSourceDirectory);

			if (!legacySourcePath.empty() && !ShaderInjectorIO::FileExists(centralSourcePath) && ShaderInjectorIO::FileExists(legacySourcePath))
				CopyFileA(legacySourcePath.c_str(), centralSourcePath.c_str(), TRUE);

			replacement.shaderSourcePath = centralSourcePath;
		}

		replacement.jsonPath = jsonPath;
	}

	bool IsShaderReplacementJsonFilename(const char* filename)
	{
		if (!filename)
			return false;

		const std::string name = filename;
		return name == "ShaderReplacement.json";
	}

	std::string ShaderTypeToString(ShaderReplacement::ShaderType shaderType)
	{
		switch (shaderType)
		{
			case ShaderReplacement::VertexShader: return "VertexShader";
			case ShaderReplacement::HullShader: return "HullShader";
			case ShaderReplacement::DomainShader: return "DomainShader";
			case ShaderReplacement::GeometryShader: return "GeometryShader";
			case ShaderReplacement::PixelShader: return "PixelShader";
			case ShaderReplacement::ComputeShader: return "ComputeShader";
			default: return "Unknown";
		}
	}

	std::string ShaderProfileForType(ShaderReplacement::ShaderType shaderType)
	{
		switch (shaderType)
		{
			case ShaderReplacement::VertexShader: return "vs_6_6";
			case ShaderReplacement::HullShader: return "hs_6_6";
			case ShaderReplacement::DomainShader: return "ds_6_6";
			case ShaderReplacement::GeometryShader: return "gs_6_6";
			case ShaderReplacement::PixelShader: return "ps_6_6";
			case ShaderReplacement::ComputeShader: return "cs_6_6";
			default: return "";
		}
	}

	bool WriteShaderReplacementJson(const ShaderReplacement::ShaderReplacementDisk& replacement)
	{
		std::ofstream file(replacement.jsonPath, std::ios::out | std::ios::trunc);

		if (!file.is_open())
			return false;

		ShaderReplacement::ShaderReplacementDisk portableReplacement = replacement;
		MakeReplacementPortableForDisk(portableReplacement);

		nlohmann::ordered_json json = portableReplacement;
		json["pipelineTemplates"] = portableReplacement.pipelineTemplates;
		file << json.dump(4);

		return !file.fail();
	}

	bool LoadShaderReplacementJson(const std::string& path, ShaderReplacement::ShaderReplacementDisk& outReplacement)
	{
		try
		{
			std::ifstream file(path);

			if (!file.is_open())
				return false;

			nlohmann::ordered_json json{};
			file >> json;
			outReplacement = json.get<ShaderReplacement::ShaderReplacementDisk>();
			if (json.contains("pipelineTemplates") && json["pipelineTemplates"].is_array())
				outReplacement.pipelineTemplates = json["pipelineTemplates"].get<std::vector<ShaderReplacement::ShaderPipelineTemplateDisk>>();

			ResolveReplacementPathsFromJsonLocation(outReplacement, path);

			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool WritePipelineStreamMetadataJson(const std::string& path, const ShaderReplacement::ShaderPipelineStreamMetadataDisk& metadata)
	{
		std::ofstream file(path, std::ios::out | std::ios::trunc);

		if (!file.is_open())
			return false;

		nlohmann::ordered_json json = metadata;
		file << json.dump(4);

		return !file.fail();
	}

	bool LoadPipelineStreamMetadataJson(const std::string& path, ShaderReplacement::ShaderPipelineStreamMetadataDisk& outMetadata)
	{
		try
		{
			std::ifstream file(path);

			if (!file.is_open())
				return false;

			nlohmann::ordered_json json{};
			file >> json;
			outMetadata = json.get<ShaderReplacement::ShaderPipelineStreamMetadataDisk>();
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	void CollectShaderReplacementJsonFiles(const std::string& directory, std::vector<std::string>& outJsonFiles)
	{
		WIN32_FIND_DATAA findData{};
		const std::string jsonPattern = directory + "\\*" + ShaderInjectorIO::extensionJSON;
		HANDLE findHandle = FindFirstFileA(jsonPattern.c_str(), &findData);

		if (findHandle != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && IsShaderReplacementJsonFilename(findData.cFileName))
					outJsonFiles.push_back(directory + "\\" + findData.cFileName);
			} while (FindNextFileA(findHandle, &findData));

			FindClose(findHandle);
		}

		const std::string childPattern = directory + "\\*";
		findHandle = FindFirstFileA(childPattern.c_str(), &findData);

		if (findHandle == INVALID_HANDLE_VALUE)
			return;

		do
		{
			if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
				continue;

			if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0)
				continue;

			const std::string childDirectory = directory + "\\" + findData.cFileName;
			const std::string childJsonPattern = childDirectory + "\\*" + ShaderInjectorIO::extensionJSON;
			WIN32_FIND_DATAA childFindData{};
			HANDLE childFindHandle = FindFirstFileA(childJsonPattern.c_str(), &childFindData);

			if (childFindHandle == INVALID_HANDLE_VALUE)
				continue;

			do
			{
				if (!(childFindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && IsShaderReplacementJsonFilename(childFindData.cFileName))
					outJsonFiles.push_back(childDirectory + "\\" + childFindData.cFileName);
			} while (FindNextFileA(childFindHandle, &childFindData));

			FindClose(childFindHandle);
		} while (FindNextFileA(findHandle, &findData));

		FindClose(findHandle);
	}
}
