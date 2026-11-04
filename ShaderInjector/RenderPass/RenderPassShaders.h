#pragma once

#include <string>
#include <memory>
#include <vector>

#include "ModifiedShader/ModifiedShader.h"
#include "RenderPass/RenderPass.h"
#include "ShaderConfiguration/CompileConfiguration.h"

namespace RenderPassShaders
{
	bool CreateShaderTemplate(
		RenderPass::RenderPassDisk& renderPass,
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		std::string& outError,
		bool compileShaders = true);
	bool CompileShaders(RenderPass::RenderPassDisk& renderPass, std::string& outError);
	bool CompilePreparedShaders(
		RenderPass::RenderPassDisk& renderPass,
		const std::vector<std::string>& compilerArguments,
		std::string& outError,
		std::shared_ptr<const ShaderConfiguration::CompileConfiguration> configuration = {},
		std::shared_ptr<const ShaderConfiguration::CompileConfiguration> previousConfiguration = {});
	void LoadShaderVariants(RenderPass::RenderPassDisk& renderPass, bool loadCachedBlobs = true);
} //namespace RenderPassShaders
