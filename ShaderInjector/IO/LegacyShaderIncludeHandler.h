#pragma once

#if defined(_WIN32)

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

#include <d3dcompiler.h>

#include "ShaderConfiguration/CompileConfiguration.h"

namespace ShaderInjectorIO
{
	//resolve legacy compiler includes beside their parent, source shader, or shared include folder.
	class LegacyShaderIncludeHandler final : public ID3DInclude
	{
		std::filesystem::path sourceDirectory;
		std::filesystem::path sharedIncludesDirectory;
		std::shared_ptr<const ShaderConfiguration::CompileConfiguration> configuration;
		std::unordered_map<LPCVOID, std::filesystem::path> openFileDirectories;
		std::string sourcePreparationError;

	  public:
		LegacyShaderIncludeHandler(
			std::filesystem::path sourceDirectoryPath,
			std::filesystem::path sharedIncludesDirectoryPath,
			std::shared_ptr<const ShaderConfiguration::CompileConfiguration> configurationSnapshot);
		~LegacyShaderIncludeHandler();
		HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE includeType, LPCSTR fileName, LPCVOID parentIncludeData, LPCVOID* outputIncludeData, UINT* outputIncludeByteCount) override;
		HRESULT STDMETHODCALLTYPE Close(LPCVOID includeData) override;
		const std::string& GetSourcePreparationError() const;
	};
} //namespace ShaderInjectorIO

#endif
