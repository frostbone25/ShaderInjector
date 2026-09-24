#pragma once

#include "Enum/ShaderModel.h"

namespace ShaderInjectorGUI
{
	//pair a shader model value with the label shown in the compiler settings combo.
	struct ShaderModelOption
	{
		Globals::ShaderModel shaderModel;
		const char* displayLabel;
	};
} //namespace ShaderInjectorGUI
