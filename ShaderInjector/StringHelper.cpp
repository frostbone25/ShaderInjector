#include "StringHelper.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

#if defined(_WIN32)
	#include <Windows.h>
#else
	#include <codecvt>
	#include <locale>
#endif

//custom 
#include "ShaderTarget.h"

namespace StringHelper
{
	std::string PointerToString(const void* ptr)
	{
		return Format("%p", ptr);
	}

	std::string SafeString(const char* text)
	{
		return text ? text : "";
	}

	std::string TrimWhitespace(const std::string& text)
	{
		const size_t firstCharacter = text.find_first_not_of(" \t\r\n");
		if (firstCharacter == std::string::npos)
			return {};

		const size_t lastCharacter = text.find_last_not_of(" \t\r\n");
		return text.substr(firstCharacter, lastCharacter - firstCharacter + 1);
	}

	std::string CollapseWhitespace(const std::string& text)
	{
		std::string normalizedText;
		normalizedText.reserve(text.size());
		bool pendingSpace = false;

		for (unsigned char character : text)
		{
			if (std::isspace(character))
			{
				pendingSpace = !normalizedText.empty();
				continue;
			}

			if (pendingSpace)
				normalizedText.push_back(' ');
			normalizedText.push_back(static_cast<char>(character));
			pendingSpace = false;
		}

		return normalizedText;
	}

	std::string LowercaseAscii(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character)
		{
			return static_cast<char>(std::tolower(character));
		});
		return text;
	}

	bool EqualsIgnoreCase(const std::string& left, const std::string& right)
	{
		return LowercaseAscii(left) == LowercaseAscii(right);
	}

	bool EndsWithIgnoreCase(const std::string& text, const std::string& suffix)
	{
		if (suffix.size() > text.size())
			return false;

		return EqualsIgnoreCase(text.substr(text.size() - suffix.size()), suffix);
	}

	std::wstring Utf8ToWide(const std::string& text, bool fallbackToActiveCodePage)
	{
		if (text.empty())
			return {};

#if defined(_WIN32)
		UINT codePage = CP_UTF8;
		DWORD conversionFlags = MB_ERR_INVALID_CHARS;
		int characterCount = MultiByteToWideChar(codePage, conversionFlags, text.c_str(), -1, nullptr, 0);

		if (characterCount == 0 && fallbackToActiveCodePage)
		{
			codePage = CP_ACP;
			conversionFlags = 0;
			characterCount = MultiByteToWideChar(codePage, conversionFlags, text.c_str(), -1, nullptr, 0);
		}

		if (characterCount == 0)
			return {};

		std::wstring wideText(static_cast<size_t>(characterCount), L'\0');
		MultiByteToWideChar(codePage, conversionFlags, text.c_str(), -1, wideText.data(), characterCount);
		wideText.pop_back();
		return wideText;
#else
		try
		{
			std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
			return converter.from_bytes(text);
		}
		catch (...)
		{
			return {};
		}
#endif
	}

	std::string WideToUtf8(const std::wstring& text)
	{
		if (text.empty())
			return {};

#if defined(_WIN32)
		const int requiredBytes = WideCharToMultiByte(
			CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
		if (requiredBytes <= 0)
			return {};

		std::string utf8Text(static_cast<size_t>(requiredBytes), '\0');
		WideCharToMultiByte(
			CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8Text.data(), requiredBytes, nullptr, nullptr);
		return utf8Text;
#else
		try
		{
			std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
			return converter.to_bytes(text);
		}
		catch (...)
		{
			return {};
		}
#endif
	}

	std::string WideToUtf8(const wchar_t* text)
	{
		return text ? WideToUtf8(std::wstring(text)) : std::string();
	}

	std::string WindowsErrorMessage(unsigned long errorCode)
	{
#if defined(_WIN32)
		char* messageBuffer = nullptr;
		const DWORD messageLength = FormatMessageA(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			static_cast<DWORD>(errorCode),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			reinterpret_cast<char*>(&messageBuffer),
			0,
			nullptr);

		std::string message = messageLength && messageBuffer
			? std::string(messageBuffer, messageLength)
			: "Windows error " + std::to_string(errorCode);

		if (messageBuffer)
			LocalFree(messageBuffer);

		while (!message.empty() && (message.back() == '\r' || message.back() == '\n'))
			message.pop_back();
		return message;
#else
		return std::strerror(static_cast<int>(errorCode));
#endif
	}

	std::string FormatHRESULT(long result)
	{
		return Format("0x%08X", static_cast<uint32_t>(result));
	}

	std::string FormatUnsignedHex(uint64_t value, int width, bool includePrefix)
	{
		std::ostringstream stream;
		if (includePrefix)
			stream << "0x";
		stream << std::uppercase << std::hex;
		if (width > 0)
			stream << std::setw(width) << std::setfill('0');
		stream << value;
		return stream.str();
	}

	std::string FormatBytesAsGiB(uint64_t bytes)
	{
		std::ostringstream stream;
		stream << std::fixed << std::setprecision(2)
			<< static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0) << " GiB";
		return stream.str();
	}

	std::string BoolText(bool value)
	{
		return value ? "true" : "false";
	}

	std::string ExecutablePathFromCommandLine(const std::string& commandLine)
	{
		const size_t commandStart = commandLine.find_first_not_of(" \t");
		if (commandStart == std::string::npos)
			return {};

		if (commandLine[commandStart] == '"')
		{
			const size_t closingQuote = commandLine.find('"', commandStart + 1);
			if (closingQuote != std::string::npos)
				return commandLine.substr(commandStart + 1, closingQuote - commandStart - 1);
		}

		const size_t commandEnd = commandLine.find_first_of(" \t", commandStart);
		return commandLine.substr(commandStart, commandEnd - commandStart);
	}

	std::string ShaderTypeToString(ShaderTarget::ShaderType shaderType)
	{
		switch (shaderType)
		{
		case ShaderTarget::VertexShader: return "VertexShader";
		case ShaderTarget::HullShader: return "HullShader";
		case ShaderTarget::DomainShader: return "DomainShader";
		case ShaderTarget::GeometryShader: return "GeometryShader";
		case ShaderTarget::PixelShader: return "PixelShader";
		case ShaderTarget::ComputeShader: return "ComputeShader";
		default: return "Unknown";
		}
	}

	std::string ShaderProfileForType(ShaderTarget::ShaderType shaderType)
	{
		switch (shaderType)
		{
		case ShaderTarget::VertexShader: return "vs_6_6";
		case ShaderTarget::HullShader: return "hs_6_6";
		case ShaderTarget::DomainShader: return "ds_6_6";
		case ShaderTarget::GeometryShader: return "gs_6_6";
		case ShaderTarget::PixelShader: return "ps_6_6";
		case ShaderTarget::ComputeShader: return "cs_6_6";
		default: return "";
		}
	}
}
