#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "ShaderTarget/ShaderTarget.h"

namespace ModifiedShaderCreation
{
	bool CreateTemplate(
		ShaderTarget::ShaderType shaderType,
		uint64_t shaderHash,
		const void* shaderBytecode,
		size_t shaderBytecodeLength,
		std::string& outMessage);
}
