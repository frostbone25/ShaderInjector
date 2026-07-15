#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

//custom 
#include "ShaderTarget.h"

namespace StringHelper
{
	template<typename... Arguments>
	std::string Format(const char* format, Arguments... arguments)
	{
		if (!format)
			return {};

		const int characterCount = std::snprintf(nullptr, 0, format, arguments...);
		if (characterCount <= 0)
			return {};

		std::string text(static_cast<size_t>(characterCount), '\0');
		std::snprintf(text.data(), text.size() + 1, format, arguments...);
		return text;
	}

	std::string PointerToString(const void* ptr);
	std::string SafeString(const char* text);
	std::string TrimWhitespace(const std::string& text);
	std::string CollapseWhitespace(const std::string& text);
	std::string LowercaseAscii(std::string text);
	bool EqualsIgnoreCase(const std::string& left, const std::string& right);
	bool EndsWithIgnoreCase(const std::string& text, const std::string& suffix);
	std::wstring Utf8ToWide(const std::string& text, bool fallbackToActiveCodePage = true);
	std::string WideToUtf8(const std::wstring& text);
	std::string WideToUtf8(const wchar_t* text);
	std::string WindowsErrorMessage(unsigned long errorCode);
	std::string FormatHRESULT(long result);
	std::string FormatUnsignedHex(uint64_t value, int width = 0, bool includePrefix = true);
	std::string FormatBytesAsGiB(uint64_t bytes);
	std::string BoolText(bool value);
	std::string ExecutablePathFromCommandLine(const std::string& commandLine);

	std::string ShaderTypeToString(ShaderTarget::ShaderType shaderType);

	std::string ShaderProfileForType(ShaderTarget::ShaderType shaderType);
}
