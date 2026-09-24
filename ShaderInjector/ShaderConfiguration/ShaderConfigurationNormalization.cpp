#include "ShaderConfiguration/ShaderConfigurationInternal.h"

#include <vector>

#include "StringHelper.h"

namespace ShaderConfiguration::Internal
{
	std::string NormalizeType(const std::string& requestedType, const std::string& defineValue)
	{
		std::string type = StringHelper::LowercaseAscii(StringHelper::TrimWhitespace(requestedType));

		if (type == "boolean")
			type = "bool";

		else if (type == "integer")
			type = "int";

		if (type == "bool" ||
			type == "int" ||
			type == "float" ||
			type == "float2" ||
			type == "float3" ||
			type == "float4")
		{
			return type;
		}

		const std::string lowercaseValue = StringHelper::LowercaseAscii(StringHelper::TrimWhitespace(defineValue));

		if (lowercaseValue.empty() || lowercaseValue == "true" || lowercaseValue == "false")
			return "bool";

		for (const char* vectorType : {"float2", "float3", "float4"})
		{
			if (StartsWith(lowercaseValue, std::string(vectorType) + "("))
				return vectorType;
		}

		int integerValue = 0;

		if (TryParseIntegerText(lowercaseValue, integerValue))
			return "int";

		float floatValue = 0.0f;

		if (TryParseFloatText(lowercaseValue, floatValue))
			return "float";

		return {};
	}

	size_t ComponentCountForType(const std::string& type)
	{
		if (type == "float2")
			return 2;

		if (type == "float3")
			return 3;

		if (type == "float4")
			return 4;

		if (type == "float")
			return 1;
		return 0;
	}

	bool ParseBooleanText(const std::string& text, bool fallbackValue)
	{
		const std::string lowercaseValue = StringHelper::LowercaseAscii(StringHelper::TrimWhitespace(text));

		if (lowercaseValue == "true" || lowercaseValue == "1")
			return true;

		if (lowercaseValue == "false" || lowercaseValue == "0")
			return false;

		return fallbackValue;
	}

	std::string NormalizeValue(
		const std::string& type,
		const std::string& value,
		const std::string& fallbackValue,
		bool fallbackBoolean)
	{
		std::string candidate = value;
		if (StringHelper::TrimWhitespace(value).empty())
			candidate = fallbackValue;

		if (type == "bool")
		{
			if (ParseBooleanText(candidate, fallbackBoolean))
				return "true";
			return "false";
		}

		if (type == "int")
		{
			int integerValue = 0;

			if (!TryParseIntegerText(candidate, integerValue))
			{
				float floatValue = 0.0f;

				if (TryParseFloatText(candidate, floatValue))
					integerValue = static_cast<int>(floatValue);
				else if (!TryParseIntegerText(fallbackValue, integerValue) && TryParseFloatText(fallbackValue, floatValue))
					integerValue = static_cast<int>(floatValue);
			}

			return std::to_string(integerValue);
		}

		const size_t componentCount = ComponentCountForType(type);

		if (componentCount == 0)
			return {};

		std::vector<float> components = ParseNumericComponents(candidate);

		if (components.size() != componentCount)
			components = ParseNumericComponents(fallbackValue);

		if (components.size() != componentCount)
			components.assign(componentCount, 0.0f);

		if (componentCount == 1)
			return FormatFloat(components.front());
		return FormatFloatComponents(components.data(), components.size());
	}
} //namespace ShaderConfiguration::Internal
