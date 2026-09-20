#pragma once

#include "ShaderConfiguration/DocumentDisk.h"
#include "ShaderConfiguration/PropertyDisk.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ShaderConfiguration
{
	bool WriteJson(const std::string& path, const DocumentDisk& document);
	bool LoadJson(const std::string& path, DocumentDisk& outDocument);

	bool ParseShaderSource(
		const std::string& relativeSourcePath,
		const std::string& sourceText,
		std::vector<PropertyDisk>& outProperties);

	bool RewriteShaderSource(
		const std::string& sourceText,
		const std::vector<const PropertyDisk*>& properties,
		std::string& outSourceText,
		std::string& outError);

	bool TryGetBoolean(const PropertyDisk& property, bool& outValue);
	bool TryGetInteger(const PropertyDisk& property, int& outValue);
	bool TryGetFloatComponents(const PropertyDisk& property, std::vector<float>& outValues);
	bool TryGetRange(const PropertyDisk& property, float& outMinimum, float& outMaximum);

	void SetBoolean(PropertyDisk& property, bool value);
	void SetInteger(PropertyDisk& property, int value);
	void SetFloatComponents(PropertyDisk& property, const float* values, size_t componentCount);
	size_t ComponentCount(const PropertyDisk& property);
}
