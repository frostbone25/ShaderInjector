#pragma once

#include <string>

#include "ModifiedShader/ModifiedShader.h"
#include "RenderPass/RenderPass.h"

namespace RenderPassShaders
{
	bool CreateFragmentShaderTemplate(
		RenderPass::RenderPassDisk& renderPass,
		const ModifiedShader::PackageDisk& modifiedShader,
		std::string& outError);
	bool CompileShaders(RenderPass::RenderPassDisk& renderPass, std::string& outError);
}
