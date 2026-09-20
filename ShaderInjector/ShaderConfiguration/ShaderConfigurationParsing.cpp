#include "ShaderConfiguration/ShaderConfiguration.h"
#include "ShaderConfiguration/ShaderConfigurationInternal.h"

#include <unordered_map>
#include <utility>

#include "IO/ShaderInjectorIO.h"
#include "StringHelper.h"

namespace ShaderConfiguration::Internal
{
	std::string MetadataValue(const std::string& comment, const std::string& tag)
	{
		const std::string lowercaseComment = StringHelper::LowercaseAscii(comment);
		const std::string lowercaseTag = StringHelper::LowercaseAscii(tag);
		const size_t tagPosition = lowercaseComment.find(lowercaseTag);

		if (tagPosition == std::string::npos)
			return {};

		const size_t colonPosition = comment.find(':', tagPosition + tag.size());

		return colonPosition == std::string::npos
			? std::string()
			: StringHelper::TrimWhitespace(comment.substr(colonPosition + 1));
	}

	ConfigurationMetadata ParseMetadata(const std::vector<std::string>& comments)
	{
		ConfigurationMetadata metadata{};

		for (const std::string& comment : comments)
		{
			const std::string lowercaseComment = StringHelper::LowercaseAscii(comment);

			if (lowercaseComment.find("[no config]") != std::string::npos)
			{
				metadata.noConfiguration = true;
				continue;
			}

			if (lowercaseComment.find("[config type]") != std::string::npos)
			{
				metadata.hasConfigurationTag = true;
				metadata.type = MetadataValue(comment, "[CONFIG TYPE]");
				continue;
			}

			if (lowercaseComment.find("[config default]") != std::string::npos)
			{
				metadata.hasConfigurationTag = true;
				metadata.defaultValue = MetadataValue(comment, "[CONFIG DEFAULT]");
				continue;
			}

			if (lowercaseComment.find("[config range]") != std::string::npos)
			{
				metadata.hasConfigurationTag = true;
				metadata.range = MetadataValue(comment, "[CONFIG RANGE]");
				continue;
			}

			metadata.descriptionLines.push_back(comment);
		}

		return metadata;
	}

	std::string JoinDescription(const std::vector<std::string>& lines)
	{
		std::string description;

		for (const std::string& line : lines)
		{
			if (!description.empty())
				description += '\n';

			description += line;
		}

		return StringHelper::TrimWhitespace(description);
	}
}

namespace ShaderConfiguration
{
	using Internal::ConfigurationMetadata;
	using Internal::JoinDescription;
	using Internal::NormalizeType;
	using Internal::ParseBooleanText;
	using Internal::ParseMetadata;
	using Internal::ParsedDefine;
	using Internal::SourceLine;
	using Internal::SplitSourceLines;
	using Internal::TryParseComment;
	using Internal::TryParseDefine;
	using Internal::NormalizeValue;

	bool ParseShaderSource(
		const std::string& relativeSourcePath,
		const std::string& sourceText,
		std::vector<PropertyDisk>& outProperties)
	{
		const std::vector<SourceLine> lines = SplitSourceLines(sourceText);
		std::vector<std::string> pendingComments;
		std::unordered_map<std::string, int> definitionCounts;
		int sourceOrder = 0;

		for (const SourceLine& line : lines)
		{
			ParsedDefine define{};

			if (TryParseDefine(line.content, define))
			{
				const ConfigurationMetadata metadata = ParseMetadata(pendingComments);
				const int definitionIndex = definitionCounts[define.name]++;
				pendingComments.clear();

				if (metadata.noConfiguration)
					continue;

				const std::string type = NormalizeType(metadata.type, define.value);

				if (type.empty())
					continue;

				const bool usesDefinitionPresence = type == "bool" && define.value.empty();
				const bool currentBoolean = usesDefinitionPresence
					? !define.commentedOut
					: ParseBooleanText(define.value, !define.commentedOut);
				const std::string sourceFallback = usesDefinitionPresence
					? (currentBoolean ? "true" : "false")
					: define.value;
				const std::string normalizedDefault = NormalizeValue(
					type,
					metadata.defaultValue,
					sourceFallback,
					currentBoolean);
				const std::string normalizedValue = NormalizeValue(
					type,
					sourceFallback,
					normalizedDefault,
					currentBoolean);

				PropertyDisk property{};
				property.name = define.name;
				property.sourceFile = ShaderInjectorIO::FileNameFromPath(relativeSourcePath);
				property.sourcePath = relativeSourcePath;
				property.comment = JoinDescription(metadata.descriptionLines);
				property.type = type;
				property.defaultValue = normalizedDefault;
				property.value = normalizedValue;
				property.range = StringHelper::TrimWhitespace(metadata.range);
				property.booleanUsesDefinitionPresence = usesDefinitionPresence;
				property.definitionIndex = definitionIndex;
				property.sourceOrder = sourceOrder++;
				property.id = relativeSourcePath + "::" + define.name + "::" + std::to_string(definitionIndex);
				outProperties.push_back(std::move(property));
				continue;
			}

			std::string comment;

			if (TryParseComment(line.content, comment))
			{
				pendingComments.push_back(comment);
				continue;
			}

			pendingComments.clear();
		}

		return true;
	}
}
