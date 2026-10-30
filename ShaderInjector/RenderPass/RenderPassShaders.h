#pragma once

#include <string>
#include <vector>

#include "ModifiedShader/ModifiedShader.h"
#include "RenderPass/RenderPass.h"

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
		std::string& outError);
	void LoadShaderVariants(RenderPass::RenderPassDisk& renderPass);
} //namespace RenderPassShaders
