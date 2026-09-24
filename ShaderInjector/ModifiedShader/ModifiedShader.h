#pragma once

#include <string>

#include "ModifiedShader/ModifiedShaderPackageDisk.h"
#include "ModifiedShader/ModifiedShaderTargetDisk.h"
#include "ShaderTarget/ShaderTarget.h"

namespace ModifiedShader
{
	bool WriteJson(const ModifiedShaderPackageDisk& package);
	bool LoadJson(const std::string& jsonPath, ModifiedShaderPackageDisk& outPackage);
	ModifiedShaderTargetDisk BuildTargetFromShaderTarget(
		const ShaderTarget::ShaderTargetDisk& shaderTarget,
		const std::string& targetApplication = "",
		const std::string& gameVersion = "");
} //namespace ModifiedShader
