#pragma once

#include <string>

#include "ShaderTarget/ShaderTargetDisk.h"

namespace ShaderTarget::Internal
{
	void MakeReplacementPortableForDisk(ShaderTargetDisk& replacement);
	void ResolveReplacementPathsFromJsonLocation(ShaderTargetDisk& replacement, const std::string& jsonPath);
}
