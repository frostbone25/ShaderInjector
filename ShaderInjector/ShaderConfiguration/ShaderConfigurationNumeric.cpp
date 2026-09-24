#include "ShaderConfiguration/ShaderConfigurationInternal.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

#include "StringHelper.h"

namespace ShaderConfiguration::Internal
{
	bool TryParseIntegerText(const std::string& text, int& outValue)
	{
		std::string trimmed = StringHelper::TrimWhitespace(text);

		if (trimmed.size() >= 2 && trimmed.front() == '(' && trimmed.back() == ')')
			trimmed = StringHelper::TrimWhitespace(trimmed.substr(1, trimmed.size() - 2));

		while (!trimmed.empty() &&
			   (trimmed.back() == 'u' || trimmed.back() == 'U' ||
				trimmed.back() == 'l' || trimmed.back() == 'L'))
		{
			trimmed.pop_back();
		}

		if (trimmed.empty())
			return false;

		char* parseEnd = nullptr;
		errno = 0;
		const long value = std::strtol(trimmed.c_str(), &parseEnd, 10);

		if (errno != 0 || parseEnd == trimmed.c_str() ||
			*parseEnd != '\0' ||
			value < (std::numeric_limits<int>::min)() ||
			value > (std::numeric_limits<int>::max)())
		{
			return false;
		}

		outValue = static_cast<int>(value);
		return true;
	}

	bool TryParseFloatText(const std::string& text, float& outValue)
	{
		std::string trimmed = StringHelper::TrimWhitespace(text);

		if (trimmed.size() >= 2 && trimmed.front() == '(' && trimmed.back() == ')')
			trimmed = StringHelper::TrimWhitespace(trimmed.substr(1, trimmed.size() - 2));

		if (!trimmed.empty() &&
			(trimmed.back() == 'f' || trimmed.back() == 'F' ||
			 trimmed.back() == 'h' || trimmed.back() == 'H'))
		{
			trimmed.pop_back();
		}

		if (trimmed.empty())
			return false;

		char* parseEnd = nullptr;
		errno = 0;
		const float value = std::strtof(trimmed.c_str(), &parseEnd);

		if (errno != 0 || parseEnd == trimmed.c_str() || *parseEnd != '\0' || !std::isfinite(value))
			return false;

		outValue = value;
		return true;
	}

	std::vector<float> ParseNumericComponents(const std::string& text)
	{
		std::string numericText = StringHelper::TrimWhitespace(text);
		const size_t openingParenthesis = numericText.find('(');
		const size_t closingParenthesis = numericText.find_last_of(')');

		if (openingParenthesis != std::string::npos && closingParenthesis > openingParenthesis)
			numericText = numericText.substr(openingParenthesis + 1, closingParenthesis - openingParenthesis - 1);

		if (!numericText.empty() && numericText.front() == '[' && numericText.back() == ']')
			numericText = numericText.substr(1, numericText.size() - 2);

		std::replace(numericText.begin(), numericText.end(), ',', ' ');
		std::istringstream stream(numericText);
		std::vector<float> values;
		std::string token;

		while (stream >> token)
		{
			float value = 0.0f;

			if (!TryParseFloatText(token, value))
				return {};

			values.push_back(value);
		}

		return values;
	}

	std::string FormatFloat(float value)
	{
		std::ostringstream stream;
		stream << std::fixed << std::setprecision(6) << value;
		std::string text = stream.str();

		while (text.size() > 2 && text.back() == '0')
			text.pop_back();

		if (!text.empty() && text.back() == '.')
			text.push_back('0');

		return text;
	}

	std::string FormatFloatComponents(const float* values, size_t componentCount)
	{
		std::string text = "[";

		for (size_t index = 0; index < componentCount; ++index)
		{
			if (index != 0)
				text += ", ";
			text += FormatFloat(values[index]);
		}

		text += "]";

		return text;
	}
} //namespace ShaderConfiguration::Internal
