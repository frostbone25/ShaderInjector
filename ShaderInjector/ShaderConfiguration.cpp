#include "ShaderConfiguration.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ShaderInjectorIO.h"
#include "StringHelper.h"

namespace ShaderConfiguration
{
	namespace
	{
		struct SourceLine
		{
			std::string content;
			std::string ending;
		};

		struct ParsedDefine
		{
			std::string name;
			std::string value;
			bool commentedOut = false;
		};

		struct ConfigurationMetadata
		{
			bool noConfiguration = false;
			bool hasConfigurationTag = false;
			std::string type;
			std::string defaultValue;
			std::string range;
			std::vector<std::string> descriptionLines;
		};

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

				lines.push_back({
					sourceText.substr(lineStart, contentEnd - lineStart),
					ending });
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

			if (line.size() > directiveLength &&
				!std::isspace(
					static_cast<unsigned char>(line[directiveLength])))
			{
				return false;
			}

			std::string definition =
				StringHelper::TrimWhitespace(line.substr(directiveLength));
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

		std::string NormalizeType(const std::string& requestedType, const std::string& defineValue)
		{
			std::string type = StringHelper::LowercaseAscii(StringHelper::TrimWhitespace(requestedType));
			if (type == "boolean")
				type = "bool";
			else if (type == "integer")
				type = "int";

			if (type == "bool" || type == "int" || type == "float" ||
				type == "float2" || type == "float3" || type == "float4")
			{
				return type;
			}

			const std::string lowercaseValue = StringHelper::LowercaseAscii(StringHelper::TrimWhitespace(defineValue));
			if (lowercaseValue.empty() || lowercaseValue == "true" || lowercaseValue == "false")
				return "bool";

			for (const char* vectorType : { "float2", "float3", "float4" })
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
			return type == "float" ? 1 : 0;
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
			const std::string candidate = StringHelper::TrimWhitespace(value).empty()
				? fallbackValue
				: value;

			if (type == "bool")
				return ParseBooleanText(candidate, fallbackBoolean) ? "true" : "false";

			if (type == "int")
			{
				int integerValue = 0;
				if (!TryParseIntegerText(candidate, integerValue))
				{
					float floatValue = 0.0f;
					if (TryParseFloatText(candidate, floatValue))
						integerValue = static_cast<int>(floatValue);
					else if (!TryParseIntegerText(fallbackValue, integerValue) &&
						TryParseFloatText(fallbackValue, floatValue))
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

			return componentCount == 1
				? FormatFloat(components.front())
				: FormatFloatComponents(components.data(), components.size());
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

		std::string PropertyKey(const std::string& name, int definitionIndex)
		{
			return name + "\n" + std::to_string(definitionIndex);
		}

		std::string LeadingWhitespaceAndBom(const std::string& sourceLine)
		{
			size_t prefixLength = 0;
			if (sourceLine.size() >= 3 &&
				static_cast<unsigned char>(sourceLine[0]) == 0xEF &&
				static_cast<unsigned char>(sourceLine[1]) == 0xBB &&
				static_cast<unsigned char>(sourceLine[2]) == 0xBF)
			{
				prefixLength = 3;
			}

			while (prefixLength < sourceLine.size() &&
				(sourceLine[prefixLength] == ' ' || sourceLine[prefixLength] == '\t'))
			{
				++prefixLength;
			}

			return sourceLine.substr(0, prefixLength);
		}

		std::string DefineValueForProperty(const PropertyDisk& property)
		{
			if (property.type == "bool")
				return property.value;

			if (property.type == "int")
				return property.value;

			const size_t componentCount = ComponentCountForType(property.type);
			if (componentCount <= 1)
				return property.value;

			std::vector<float> components;
			if (!TryGetFloatComponents(property, components) || components.size() != componentCount)
				components.assign(componentCount, 0.0f);

			std::string value = property.type + "(";
			for (size_t index = 0; index < components.size(); ++index)
			{
				if (index != 0)
					value += ", ";
				value += FormatFloat(components[index]);
			}
			value += ")";
			return value;
		}
	}

	bool WriteJson(const std::string& path, const DocumentDisk& document)
	{
		if (path.empty())
			return false;

		const nlohmann::ordered_json json = document;
		return ShaderInjectorIO::WriteTextFile(path, json.dump(4));
	}

	bool LoadJson(const std::string& path, DocumentDisk& outDocument)
	{
		try
		{
			std::string jsonText;
			if (!ShaderInjectorIO::ReadTextFile(path, jsonText))
				return false;

			const nlohmann::ordered_json json = nlohmann::ordered_json::parse(jsonText);
			DocumentDisk document = json.get<DocumentDisk>();
			if (document.format != formatName ||
				document.schemaVersion != currentSchemaVersion)
			{
				return false;
			}

			outDocument = std::move(document);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

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

	bool RewriteShaderSource(
		const std::string& sourceText,
		const std::vector<const PropertyDisk*>& properties,
		std::string& outSourceText,
		std::string& outError)
	{
		outError.clear();
		std::unordered_map<std::string, const PropertyDisk*> propertyByDefinition;
		for (const PropertyDisk* property : properties)
		{
			if (property)
				propertyByDefinition[PropertyKey(property->name, property->definitionIndex)] = property;
		}

		std::unordered_set<std::string> rewrittenProperties;
		std::unordered_map<std::string, int> definitionCounts;
		const std::vector<SourceLine> lines = SplitSourceLines(sourceText);
		std::string rewrittenSource;
		rewrittenSource.reserve(sourceText.size() + properties.size() * 8);

		for (const SourceLine& line : lines)
		{
			std::string content = line.content;
			ParsedDefine define{};

			if (TryParseDefine(content, define))
			{
				const int definitionIndex = definitionCounts[define.name]++;
				const std::string key = PropertyKey(define.name, definitionIndex);
				const auto propertyIterator = propertyByDefinition.find(key);

				if (propertyIterator != propertyByDefinition.end())
				{
					const PropertyDisk& property = *propertyIterator->second;
					const std::string prefix = LeadingWhitespaceAndBom(content);

					if (property.type == "bool" && property.booleanUsesDefinitionPresence)
					{
						bool enabled = true;
						TryGetBoolean(property, enabled);
						content = prefix + (enabled ? "#define " : "// #define ") + property.name;
					}
					else
					{
						std::string defineValue = DefineValueForProperty(property);
						if (property.type == "bool")
						{
							bool enabled = false;
							TryGetBoolean(property, enabled);
							defineValue = enabled ? "1" : "0";
						}

						content = prefix + "#define " + property.name;
						if (!defineValue.empty())
							content += " " + defineValue;
					}

					rewrittenProperties.insert(property.id);
				}
			}

			rewrittenSource += content;
			rewrittenSource += line.ending;
		}

		for (const PropertyDisk* property : properties)
		{
			if (property && rewrittenProperties.find(property->id) == rewrittenProperties.end())
			{
				outError = "Could not locate #define " + property->name +
					" in " + property->sourcePath;
				return false;
			}
		}

		outSourceText = std::move(rewrittenSource);
		return true;
	}

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
