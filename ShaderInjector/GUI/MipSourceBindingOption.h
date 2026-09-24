#pragma once

#include <cstdint>
#include <string>

namespace ShaderInjectorGUI
{
	//offer a mip source texture at a particular shader register and space.
	struct MipSourceBindingOption
	{
		std::string name;
		uint32_t shaderRegister = 0;
		uint32_t registerSpace = 0;
	};
} //namespace ShaderInjectorGUI
