#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ShaderConfiguration/ConfigurationMetadata.h"
#include "ShaderConfiguration/ParsedDefine.h"
#include "ShaderConfiguration/SourceLine.h"

namespace ShaderConfiguration::Internal
{
	bool StartsWith(const std::string& text, const std::string& prefix);

	std::vector<SourceLine> SplitSourceLines(const std::string& sourceText);

	bool TryParseDefine(const std::string& sourceLine, ParsedDefine& outDefine);
	bool TryParseComment(const std::string& sourceLine, std::string& outComment);
	bool TryParseIntegerText(const std::string& text, int& outValue);
	bool TryParseFloatText(const std::string& text, float& outValue);

	std::vector<float> ParseNumericComponents(const std::string& text);
	std::string NormalizeType(const std::string& requestedType, const std::string& defineValue);
	bool ParseBooleanText(const std::string& text, bool fallbackValue);
	std::string NormalizeValue(const std::string& type, const std::string& value, const std::string& fallbackValue, bool fallbackBoolean);
	size_t ComponentCountForType(const std::string& type);
	std::string FormatFloat(float value);
	std::string FormatFloatComponents(const float* values, size_t componentCount);
} //namespace ShaderConfiguration::Internal
