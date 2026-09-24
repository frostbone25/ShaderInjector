#include "LegacyShaderIncludeHandler.h"

#if defined(_WIN32)

#include <cstdint>
#include <fstream>
#include <limits>
#include <new>
#include <utility>
#include <vector>

#include "ShaderInjectorIO.h"

namespace ShaderInjectorIO
{
	LegacyShaderIncludeHandler::LegacyShaderIncludeHandler(std::filesystem::path sourceDirectoryPath, std::filesystem::path sharedIncludesDirectoryPath)
		: sourceDirectory(std::move(sourceDirectoryPath)), sharedIncludesDirectory(std::move(sharedIncludesDirectoryPath))
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
			size_t includeAllocationSize = 1;

			if (includeFileSize > 0)
				includeAllocationSize = static_cast<size_t>(includeFileSize);

			//the compiler owns this byte buffer until it calls Close after compilation.
			char* includeData = new (std::nothrow) char[includeAllocationSize];

			if (!includeData)
				return E_OUTOFMEMORY;

			if (includeFileSize > 0 && !includeFile.read(includeData, includeFileSize))
			{
				delete[] includeData;
				continue;
			}

			*outputIncludeData = includeData;
			*outputIncludeByteCount = static_cast<UINT>(includeFileSize);
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
} //namespace ShaderInjectorIO

#endif
