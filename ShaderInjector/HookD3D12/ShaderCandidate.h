#pragma once

#include <cstdint>

#include "ShaderTarget/ShaderTarget.h"

namespace HookD3D12
{
	struct ShaderCandidate
	{
		uint64_t shaderHash = 0;
		ShaderTarget::ShaderType shaderType = ShaderTarget::Unknown;
	};
} //namespace HookD3D12
