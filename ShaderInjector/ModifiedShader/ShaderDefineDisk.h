#pragma once

#include <string>

#include "JsonHelper.h"

namespace ModifiedShader
{
	//a preprocessor define passed to the shader compiler (DXC -D name=value, or a D3D_SHADER_MACRO).
	struct ShaderDefine
	{
		std::string name;
		std::string value;

		bool operator==(const ShaderDefine& other) const
		{
			return name == other.name && value == other.value;
		}

		bool operator!=(const ShaderDefine& other) const
		{
			return !(*this == other);
		}

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			ShaderDefine,
			name,
			value)
	};
} //namespace ModifiedShader
