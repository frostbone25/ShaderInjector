#pragma once

#include <cstdint>
#include <string>

#include "Enum/RenderPassGameResourceViewType.h"

namespace ShaderInjectorGUI
{
	//describe a game texture the pass editor can bind as an input.
	struct GameTextureBindingOption
	{
		std::string name;
		RenderPass::GameResourceViewType viewType = RenderPass::GameResourceViewType::UnorderedAccess;
		uint32_t shaderRegister = 0;
		uint32_t registerSpace = 0;
	};
} //namespace ShaderInjectorGUI
