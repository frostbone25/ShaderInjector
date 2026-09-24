#pragma once

#if defined(_WIN32)

#include <filesystem>
#include <unordered_map>

#include <d3dcompiler.h>

namespace ShaderInjectorIO
{
	//resolve legacy compiler includes beside their parent, source shader, or shared include folder.
	class LegacyShaderIncludeHandler final : public ID3DInclude
	{
		std::filesystem::path sourceDirectory;
		std::filesystem::path sharedIncludesDirectory;
		std::unordered_map<LPCVOID, std::filesystem::path> openFileDirectories;

	  public:
		LegacyShaderIncludeHandler(std::filesystem::path sourceDirectoryPath, std::filesystem::path sharedIncludesDirectoryPath);
		~LegacyShaderIncludeHandler();
		HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE includeType, LPCSTR fileName, LPCVOID parentIncludeData, LPCVOID* outputIncludeData, UINT* outputIncludeByteCount) override;
		HRESULT STDMETHODCALLTYPE Close(LPCVOID includeData) override;
	};
} //namespace ShaderInjectorIO

#endif
