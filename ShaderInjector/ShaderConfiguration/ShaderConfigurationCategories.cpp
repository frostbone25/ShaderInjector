#include "ShaderConfiguration/ShaderConfigurationCategories.h"

#include <filesystem>
#include <atomic>
#include <cstdint>
#include <utility>

#include "IO/ShaderInjectorIO.h"
#include "ShaderConfiguration/ShaderConfigurationInternal.h"
#include "StringHelper.h"

namespace ShaderConfiguration
{
	namespace
	{
		std::atomic<uint64_t> gCategoryWriteSequence = 0;
	}

	bool UpdatePropertyCategorySource(
		const std::string& sourceText,
		const PropertyDisk& property,
		const std::string& category,
		std::string& outSourceText,
		std::string& outError)
	{
		if (category.find_first_of("\r\n") != std::string::npos || category.find('\0') != std::string::npos)
		{
			outError = "Category names cannot contain line breaks or null characters.";
			return false;
		}

		std::vector<Internal::SourceLine> lines = Internal::SplitSourceLines(sourceText);
		std::vector<size_t> attachedComments;
		int definitionIndex = 0;
		size_t targetLineIndex = lines.size();
		std::vector<size_t> categoryLines;

		for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
		{
			Internal::ParsedDefine define{};

			if (Internal::TryParseDefine(lines[lineIndex].content, define))
			{
				if (define.name == property.name)
				{
					if (definitionIndex == property.definitionIndex)
					{
						targetLineIndex = lineIndex;
						for (size_t commentIndex : attachedComments)
						{
							std::string comment;
							Internal::TryParseComment(lines[commentIndex].content, comment);
							if (StringHelper::LowercaseAscii(comment).find("[config category]") != std::string::npos)
								categoryLines.push_back(commentIndex);
						}
						break;
					}

					++definitionIndex;
				}

				attachedComments.clear();
				continue;
			}

			std::string comment;
			if (Internal::TryParseComment(lines[lineIndex].content, comment))
				attachedComments.push_back(lineIndex);
			else
				attachedComments.clear();
		}

		if (targetLineIndex == lines.size())
		{
			outError = "The selected shader definition no longer exists in its source file.";
			return false;
		}

		const std::string trimmedCategory = StringHelper::TrimWhitespace(category);
		if (!categoryLines.empty())
		{
			const size_t firstCategoryLine = categoryLines.front();
			if (trimmedCategory.empty())
			{
				for (auto it = categoryLines.rbegin(); it != categoryLines.rend(); ++it)
					lines.erase(lines.begin() + *it);
			}
			else
			{
				std::string& line = lines[firstCategoryLine].content;
				const size_t markerPosition = StringHelper::LowercaseAscii(line).find("[config category]");
				const size_t colonPosition = line.find(':', markerPosition);
				if (colonPosition == std::string::npos)
				{
					outError = "The existing category metadata comment is malformed.";
					return false;
				}
				line = line.substr(0, colonPosition + 1) + " " + trimmedCategory;
				for (auto it = categoryLines.rbegin(); it != categoryLines.rend(); ++it)
				{
					if (*it != firstCategoryLine)
						lines.erase(lines.begin() + *it);
				}
			}
		}
		else if (!trimmedCategory.empty())
		{
			Internal::SourceLine categoryLine{};
			categoryLine.content = "// [CONFIG CATEGORY]: " + trimmedCategory;
			categoryLine.ending = "\n";
			for (const Internal::SourceLine& line : lines)
			{
				if (!line.ending.empty())
				{
					categoryLine.ending = line.ending;
					break;
				}
			}

			if (targetLineIndex == 0 && lines[targetLineIndex].content.size() >= 3 &&
				static_cast<unsigned char>(lines[targetLineIndex].content[0]) == 0xEF &&
				static_cast<unsigned char>(lines[targetLineIndex].content[1]) == 0xBB &&
				static_cast<unsigned char>(lines[targetLineIndex].content[2]) == 0xBF)
			{
				categoryLine.content.insert(0, lines[targetLineIndex].content.substr(0, 3));
				lines[targetLineIndex].content.erase(0, 3);
			}

			lines.insert(lines.begin() + targetLineIndex, std::move(categoryLine));
		}

		std::string rewritten;
		for (const Internal::SourceLine& line : lines)
			rewritten += line.content + line.ending;

		outSourceText = std::move(rewritten);
		outError.clear();
		return true;
	}

	bool WritePropertyCategory(const PropertyDisk& property, const std::string& category, std::string& outError)
	{
		const std::filesystem::path relativePath(property.sourcePath);
		if (property.sourcePath.empty() || relativePath.is_absolute())
		{
			outError = "The shader source path must be relative to ModifiedShaders.";
			return false;
		}

		for (const auto& component : relativePath)
		{
			if (component == "..")
			{
				outError = "The shader source path escapes ModifiedShaders.";
				return false;
			}
		}

		const std::string sourcePath = ShaderInjectorIO::JoinPath(ShaderInjectorIO::GetModifiedShadersDirectory(), property.sourcePath);
		std::string sourceText;
		if (!ShaderInjectorIO::ReadTextFile(sourcePath, sourceText))
		{
			outError = "Could not read the shader source file.";
			return false;
		}

		std::string updatedSourceText;
		if (!UpdatePropertyCategorySource(sourceText, property, category, updatedSourceText, outError))
			return false;

		const std::string temporaryPath = sourcePath + ".category-staging-" +
			std::to_string(gCategoryWriteSequence.fetch_add(1, std::memory_order_relaxed)) + ".tmp";
		if (!ShaderInjectorIO::WriteTextFile(temporaryPath, updatedSourceText))
		{
			outError = "Could not stage the updated shader source file.";
			return false;
		}

		if (!ShaderInjectorIO::MovePath(temporaryPath, sourcePath, true))
		{
			outError = "Could not replace the shader source file with its staged category update.";
			return false;
		}

		return true;
	}
} //namespace ShaderConfiguration
