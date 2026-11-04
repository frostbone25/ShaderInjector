#include "LegacyShaderIncludeHandler.h"

#if defined(_WIN32)

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <new>
#include <utility>
#include <vector>

#include "ShaderInjectorIO.h"

namespace ShaderInjectorIO
{
	LegacyShaderIncludeHandler::LegacyShaderIncludeHandler(
		std::filesystem::path sourceDirectoryPath,
		std::filesystem::path sharedIncludesDirectoryPath,
		std::shared_ptr<const ShaderConfiguration::CompileConfiguration> configurationSnapshot)
		: sourceDirectory(std::move(sourceDirectoryPath)),
		  sharedIncludesDirectory(std::move(sharedIncludesDirectoryPath)),
		  configuration(std::move(configurationSnapshot))
	{
	}

	LegacyShaderIncludeHandler::~LegacyShaderIncludeHandler()
	{
		//the compiler normally closes each include; release any buffer still tracked on an error path.
		for (const auto& openIncludeFile : openFileDirectories)
			delete[] static_cast<const char*>(openIncludeFile.first);
	}

	HRESULT STDMETHODCALLTYPE LegacyShaderIncludeHandler::Open(D3D_INCLUDE_TYPE, LPCSTR fileName, LPCVOID parentIncludeData, LPCVOID* outputIncludeData, UINT* outputIncludeByteCount)
	{
		if (!fileName || !outputIncludeData || !outputIncludeByteCount)
			return E_INVALIDARG;

		std::vector<std::filesystem::path> candidateIncludePaths;
		const std::filesystem::path includePath = PathFromUTF8(fileName);

		if (includePath.is_absolute())
			candidateIncludePaths.push_back(includePath);
		else
		{
			//try beside the parent include first, then beside the shader, then in shared includes.
			const auto parentIncludeDirectory = openFileDirectories.find(parentIncludeData);

			if (parentIncludeDirectory != openFileDirectories.end())
				candidateIncludePaths.push_back(parentIncludeDirectory->second / includePath);

			candidateIncludePaths.push_back(sourceDirectory / includePath);
			candidateIncludePaths.push_back(sharedIncludesDirectory / includePath);
		}

		for (const std::filesystem::path& candidateIncludePath : candidateIncludePaths)
		{
			std::ifstream includeFile(candidateIncludePath, std::ios::binary | std::ios::ate);

			if (!includeFile.is_open())
				continue;

			const std::streamsize includeFileSize = includeFile.tellg();

			if (includeFileSize < 0 || static_cast<uint64_t>(includeFileSize) > (std::numeric_limits<UINT>::max)())
				continue;

			includeFile.seekg(0, std::ios::beg);
			std::string includeText;
			includeText.resize(static_cast<size_t>(includeFileSize));

			if (includeFileSize > 0 && !includeFile.read(includeText.data(), includeFileSize))
			{
				continue;
			}

			if (configuration)
			{
				const std::string includeFilePath = PathToUTF8(candidateIncludePath);
				const std::string relativeSourcePath = MakeRelativePath(includeFilePath, GetModifiedShadersDirectory());
				std::string preparedIncludeText;
				std::string preparationError;

				if (!configuration->PrepareSource(relativeSourcePath, includeText, preparedIncludeText, preparationError))
				{
					sourcePreparationError = preparationError;
					return E_FAIL;
				}

				includeText = std::move(preparedIncludeText);
			}

			if (includeText.size() > (std::numeric_limits<UINT>::max)())
				return E_FAIL;

			const size_t includeAllocationSize = (std::max)(includeText.size(), size_t(1));
			char* includeData = new (std::nothrow) char[includeAllocationSize];

			if (!includeData)
				return E_OUTOFMEMORY;

			if (!includeText.empty())
				std::copy(includeText.begin(), includeText.end(), includeData);

			*outputIncludeData = includeData;
			*outputIncludeByteCount = static_cast<UINT>(includeText.size());
			openFileDirectories[includeData] = candidateIncludePath.parent_path();
			return S_OK;
		}

		return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
	}

	HRESULT STDMETHODCALLTYPE LegacyShaderIncludeHandler::Close(LPCVOID includeData)
	{
		openFileDirectories.erase(includeData);
		delete[] static_cast<const char*>(includeData);
		return S_OK;
	}

	const std::string& LegacyShaderIncludeHandler::GetSourcePreparationError() const
	{
		return sourcePreparationError;
	}
} //namespace ShaderInjectorIO

#endif
