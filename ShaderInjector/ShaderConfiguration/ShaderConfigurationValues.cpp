#include "ShaderConfiguration/ShaderConfiguration.h"
#include "ShaderConfiguration/ShaderConfigurationInternal.h"

#include <vector>

#include "StringHelper.h"

namespace ShaderConfiguration
{
	using Internal::ComponentCountForType;
	using Internal::FormatFloat;
	using Internal::FormatFloatComponents;
	using Internal::ParseNumericComponents;
	using Internal::TryParseIntegerText;

	bool TryGetBoolean(const PropertyDisk& property, bool& outValue)
	{
		const std::string lowercaseValue = StringHelper::LowercaseAscii(StringHelper::TrimWhitespace(property.value));

		if (lowercaseValue == "true" || lowercaseValue == "1")
		{
			outValue = true;
			return true;
		}

		if (lowercaseValue == "false" || lowercaseValue == "0")
		{
			outValue = false;
			return true;
		}

		return false;
	}

	bool TryGetInteger(const PropertyDisk& property, int& outValue)
	{
		return TryParseIntegerText(property.value, outValue);
	}

	bool TryGetFloatComponents(const PropertyDisk& property, std::vector<float>& outValues)
	{
		outValues = ParseNumericComponents(property.value);
		return outValues.size() == ComponentCount(property);
	}

	bool TryGetRange(const PropertyDisk& property, float& outMinimum, float& outMaximum)
	{
		const std::vector<float> range = ParseNumericComponents(property.range);

		if (range.size() < 2 || range[0] >= range[1])
			return false;

		outMinimum = range[0];
		outMaximum = range[1];
		return true;
	}

	void SetBoolean(PropertyDisk& property, bool value)
	{
		property.value = value ? "true" : "false";
	}

	void SetInteger(PropertyDisk& property, int value)
	{
		property.value = std::to_string(value);
	}

	void SetFloatComponents(PropertyDisk& property, const float* values, size_t componentCount)
	{
		if (!values || componentCount == 0)
			return;

		property.value = componentCount == 1
			? FormatFloat(values[0])
			: FormatFloatComponents(values, componentCount);
	}

	size_t ComponentCount(const PropertyDisk& property)
	{
		return ComponentCountForType(property.type);
	}
}
