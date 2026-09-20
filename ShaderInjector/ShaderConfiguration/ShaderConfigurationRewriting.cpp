#include "ShaderConfiguration/ShaderConfiguration.h"
#include "ShaderConfiguration/ShaderConfigurationInternal.h"

#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "StringHelper.h"

namespace ShaderConfiguration::Internal
{
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

		while (prefixLength < sourceLine.size() && (sourceLine[prefixLength] == ' ' || sourceLine[prefixLength] == '\t'))
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

namespace ShaderConfiguration
{
	using Internal::DefineValueForProperty;
	using Internal::LeadingWhitespaceAndBom;
	using Internal::ParsedDefine;
	using Internal::PropertyKey;
	using Internal::SourceLine;
	using Internal::SplitSourceLines;
	using Internal::TryParseDefine;

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
				outError = "Could not locate #define " + property->name + " in " + property->sourcePath;
				return false;
			}
		}

		outSourceText = std::move(rewrittenSource);
		return true;
	}
}
