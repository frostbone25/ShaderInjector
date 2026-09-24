#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <string>

#include "Enum/RenderPassRuntimeRootBindingType.h"

namespace RenderPassRuntime
{
	const char* RootBindingTypeName(RootBindingType type);
}

std::string GetProcessorArchitectureName(WORD architecture);

std::string D3DFeatureLevelToString(D3D_FEATURE_LEVEL featureLevel);

std::string D3DShaderModelToString(D3D_SHADER_MODEL shaderModel);
