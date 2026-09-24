#pragma once

#include <cstdint>

namespace ShaderInjectorGUI
{
	//show a readable option name while storing its numeric API value.
	struct NamedUnsignedValue
	{
		uint32_t value = 0;
		const char* name = "";
	};
} //namespace ShaderInjectorGUI
