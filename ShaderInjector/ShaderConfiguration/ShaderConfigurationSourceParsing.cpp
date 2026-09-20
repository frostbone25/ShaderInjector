#include "ShaderConfiguration/ShaderConfigurationInternal.h"

#include <cctype>

#include "StringHelper.h"

namespace ShaderConfiguration::Internal
{
	bool StartsWith(const std::string& text, const std::string& prefix)
	{
		return text.size() >= prefix.size() &&
			text.compare(0, prefix.size(), prefix) == 0;
	}

	std::vector<SourceLine> SplitSourceLines(const std::string& sourceText)
	{
		std::vector<SourceLine> lines;
		size_t lineStart = 0;

		while (lineStart < sourceText.size())
		{
			const size_t newline = sourceText.find('\n', lineStart);

			if (newline == std::string::npos)
			{
				lines.push_back({ sourceText.substr(lineStart), {} });
				break;
			}

			size_t contentEnd = newline;
			std::string ending = "\n";

			if (contentEnd > lineStart && sourceText[contentEnd - 1] == '\r')
			{
				--contentEnd;
				ending = "\r\n";
			}

			lines.push_back({sourceText.substr(lineStart, contentEnd - lineStart), ending });
			lineStart = newline + 1;
		}

		if (sourceText.empty())
			lines.push_back({});

		return lines;
	}

	std::string RemoveUtf8Bom(std::string text)
	{
		if (text.size() >= 3 &&
			static_cast<unsigned char>(text[0]) == 0xEF &&
			static_cast<unsigned char>(text[1]) == 0xBB &&
			static_cast<unsigned char>(text[2]) == 0xBF)
		{
			text.erase(0, 3);
		}

		return text;
	}

	bool TryParseDefine(const std::string& sourceLine, ParsedDefine& outDefine)
	{
		std::string line = StringHelper::TrimWhitespace(RemoveUtf8Bom(sourceLine));
		bool commentedOut = false;

		if (StartsWith(line, "//"))
		{
			commentedOut = true;
			line = StringHelper::TrimWhitespace(line.substr(2));
		}

		size_t directiveLength = 0;

		if (StartsWith(line, "#define"))
			directiveLength = 7;
		else if (commentedOut && StartsWith(line, "define"))
			directiveLength = 6;
		else
			return false;

		if (line.size() > directiveLength && !std::isspace(static_cast<unsigned char>(line[directiveLength])))
		{
			return false;
		}

		std::string definition = StringHelper::TrimWhitespace(line.substr(directiveLength));

		if (definition.empty())
			return false;

		size_t nameEnd = 0;

		while (nameEnd < definition.size())
		{
			const unsigned char character = static_cast<unsigned char>(definition[nameEnd]);

			if (!std::isalnum(character) && character != '_')
				break;

			++nameEnd;
		}

		if (nameEnd == 0 || (nameEnd < definition.size() && definition[nameEnd] == '('))
			return false;

		std::string value = StringHelper::TrimWhitespace(definition.substr(nameEnd));

		const size_t inlineComment = value.find("//");

		if (inlineComment != std::string::npos)
			value = StringHelper::TrimWhitespace(value.substr(0, inlineComment));

		outDefine.name = definition.substr(0, nameEnd);
		outDefine.value = value;
		outDefine.commentedOut = commentedOut;
		return true;
	}

	bool TryParseComment(const std::string& sourceLine, std::string& outComment)
	{
		std::string line = StringHelper::TrimWhitespace(RemoveUtf8Bom(sourceLine));

		if (!StartsWith(line, "//"))
			return false;

		line = StringHelper::TrimWhitespace(line.substr(2));

		if (StartsWith(line, "#define"))
			return false;

		outComment = line;
		return true;
	}
}
