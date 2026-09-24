#pragma once

#include <cstddef>

namespace ShaderInjectorGUI
{
	//count the bindings a pass inherits from its linked modified shader.
	struct InheritedBindingSummary
	{
		size_t shaderResourceCount = 0;
		size_t constantBufferCount = 0;
	};
} //namespace ShaderInjectorGUI
