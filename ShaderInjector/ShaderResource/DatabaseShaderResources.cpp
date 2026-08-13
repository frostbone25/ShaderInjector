#include "ShaderResource/DatabaseShaderResources.h"

#include <algorithm>
#include <filesystem>

#include "IO/ShaderInjectorIO.h"

namespace DatabaseShaderResources
{
	namespace
	{
		std::vector<ShaderResource::TextureDisk> gShaderResources;
		bool gShaderResourcesLoaded = false;
	}

	void RefreshShaderResources()
	{
		gShaderResources.clear();
		gShaderResourcesLoaded = true;
		const std::string rootDirectory = ShaderInjectorIO::GetShaderResourcesDirectory();
		ShaderInjectorIO::DirectoryCreate(rootDirectory);

		std::vector<std::string> texturePaths;
		ShaderInjectorIO::CollectFilesByExtension(rootDirectory, ".dds", texturePaths, true, true);
		const std::filesystem::path rootPath = std::filesystem::u8path(rootDirectory);
		for (const std::string& texturePath : texturePaths)
		{
			std::error_code error;
			const std::filesystem::path path = std::filesystem::u8path(texturePath);
			std::filesystem::path relativePath = std::filesystem::relative(path, rootPath, error);
			if (error)
				continue;

			ShaderResource::TextureDisk resource{};
			resource.id = relativePath.generic_u8string();
			resource.name = path.stem().u8string();
			resource.fileName = path.filename().u8string();
			resource.filePath = texturePath;
			gShaderResources.push_back(std::move(resource));
		}

		std::sort(gShaderResources.begin(), gShaderResources.end(), [](const auto& left, const auto& right)
		{
			return left.id < right.id;
		});
		ShaderInjectorIO::WriteToLogFile(
			"DatabaseShaderResources->RefreshShaderResources: loaded DDS textures=" +
			std::to_string(gShaderResources.size()));
	}

	void EnsureShaderResourcesLoaded()
	{
		if (!gShaderResourcesLoaded)
			RefreshShaderResources();
	}

	const std::vector<ShaderResource::TextureDisk>& GetShaderResources()
	{
		EnsureShaderResourcesLoaded();
		return gShaderResources;
	}

	const ShaderResource::TextureDisk* FindShaderResourceById(const std::string& resourceId)
	{
		EnsureShaderResourcesLoaded();
		const auto resourceIt = std::find_if(gShaderResources.begin(), gShaderResources.end(), [&](const auto& resource)
		{
			return resource.id == resourceId;
		});
		return resourceIt != gShaderResources.end() ? &*resourceIt : nullptr;
	}
}
