#pragma once

#include <string>

#include "ShaderConfiguration/PropertyDisk.h"

namespace ShaderConfiguration
{
	bool UpdatePropertyCategorySource(
		const std::string& sourceText,
		const PropertyDisk& property,
		const std::string& category,
		std::string& outSourceText,
		std::string& outError);

	bool WritePropertyCategory(const PropertyDisk& property, const std::string& category, std::string& outError);
} //namespace ShaderConfiguration
