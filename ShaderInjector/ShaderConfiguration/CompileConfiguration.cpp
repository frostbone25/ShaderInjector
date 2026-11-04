#include "ShaderConfiguration/CompileConfiguration.h"

#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <set>
#include <unordered_map>
#include <utility>

#include "IO/ShaderInjectorIO.h"
#include "ShaderConfiguration/ShaderConfiguration.h"
#include "ShaderConfiguration/ShaderConfigurationInternal.h"
#include "ShaderConfiguration/ShaderSourceDependencies.h"

namespace ShaderConfiguration
{
	using Internal::FormatFloat;
	using Internal::SplitSourceLines;
	using Internal::TryParseDefine;
	using Internal::SourceLine;
	using Internal::ParsedDefine;

	namespace
	{
		uint64_t HashText(const std::string& text)
		{
			uint64_t hash = 14695981039346656037ull;

			for (const unsigned char character : text)
			{
				hash ^= character;
				hash *= 1099511628211ull;
			}

			return hash;
		}

		std::string FormatHash(uint64_t hash)
		{
			std::ostringstream stream;
			stream.imbue(std::locale::classic());
			stream << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << hash;
			return stream.str();
		}

		std::string NormalizeSourcePath(std::string sourcePath)
		{
			std::replace(sourcePath.begin(), sourcePath.end(), '\\', '/');

			for (char& character : sourcePath)
			{
				if (character >= 'A' && character <= 'Z')
					character = static_cast<char>(character - 'A' + 'a');
			}

			return sourcePath;
		}

		std::string NormalizeAbsoluteSourcePath(const std::filesystem::path& sourcePath)
		{
			std::error_code error;
			std::filesystem::path absolutePath = std::filesystem::absolute(sourcePath, error);

			if (error)
				absolutePath = sourcePath;
			else
			{
				std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(absolutePath, error);

				if (!error)
					absolutePath = std::move(canonicalPath);
			}

			return NormalizeSourcePath(absolutePath.lexically_normal().generic_u8string());
		}

		std::string NormalizeLookupPath(const std::string& shaderSourcePath, const std::string& modifiedShadersDirectory)
		{
			std::filesystem::path sourcePath = std::filesystem::u8path(shaderSourcePath);

			if (!sourcePath.is_absolute())
				sourcePath = std::filesystem::u8path(modifiedShadersDirectory) / sourcePath;

			return NormalizeSourcePath(sourcePath.lexically_normal().generic_u8string());
		}

		std::string PropertyMacroValue(const PropertyDisk& property)
		{
			if (property.valueType == "bool")
			{
				bool enabled = false;
				TryGetBoolean(property, enabled);

				if (enabled)
					return "1";

				return "0";
			}

			if (property.valueType == "int")
			{
				int integerValue = 0;

				if (TryGetInteger(property, integerValue))
					return std::to_string(integerValue);

				return "0";
			}

			const size_t componentCount = ComponentCount(property);

			if (componentCount == 0)
				return "0";

			std::vector<float> components;

			if (!TryGetFloatComponents(property, components) || components.size() != componentCount)
				components.assign(componentCount, 0.0f);

			if (componentCount == 1)
				return FormatFloat(components.front());

			std::string value = property.valueType + "(";

			for (size_t index = 0; index < components.size(); ++index)
			{
				if (index != 0)
					value += ", ";

				value += FormatFloat(components[index]);
			}

			value += ")";
			return value;
		}

		std::string SourcePrefix(const std::string& line)
		{
			size_t prefixLength = 0;

			if (line.size() >= 3 &&
				static_cast<unsigned char>(line[0]) == 0xEF &&
				static_cast<unsigned char>(line[1]) == 0xBB &&
				static_cast<unsigned char>(line[2]) == 0xBF)
			{
				prefixLength = 3;
			}

			while (prefixLength < line.size() && (line[prefixLength] == ' ' || line[prefixLength] == '\t'))
				++prefixLength;

			return line.substr(0, prefixLength);
		}

		std::string SourceIndent(const std::string& line)
		{
			std::string prefix = SourcePrefix(line);

			if (prefix.size() >= 3 &&
				static_cast<unsigned char>(prefix[0]) == 0xEF &&
				static_cast<unsigned char>(prefix[1]) == 0xBB &&
				static_cast<unsigned char>(prefix[2]) == 0xBF)
			{
				prefix.erase(0, 3);
			}

			return prefix;
		}
	}

	CompileConfiguration::CompileConfiguration(const DocumentDisk& document, std::shared_ptr<ConfigurationProgress> progressTracker)
		: progress(std::move(progressTracker)),
		modifiedShadersDirectory(ShaderInjectorIO::GetModifiedShadersDirectory())
	{
		std::vector<std::string> fingerprintEntries;

		for (const PropertyDisk& property : document.properties)
		{
			if (property.propertyType != PropertyType::Shader)
				continue;

			PropertyMacro macro{};
			macro.sourcePath = NormalizeSourcePath(property.sourcePath);
			macro.name = property.name;
			macro.id = property.id;
			macro.alias = "SHADER_INJECTOR_CONFIG_" + FormatHash(HashText(property.id));
			macro.value = PropertyMacroValue(property);
			macro.usesDefinitionPresence = property.booleanUsesDefinitionPresence;
			macro.definitionIndex = property.definitionIndex;
			//the alias lets compiler arguments override a source define without changing the file.
			defines.push_back(macro.alias + "=" + macro.value);
			std::string fingerprintEntry = NormalizeSourcePath(property.sourcePath) + "\n" + property.id + "\n" + macro.alias + "\n" + property.name + "\n" + std::to_string(property.definitionIndex) + "\n";
			if (property.booleanUsesDefinitionPresence)
				fingerprintEntry += "presence\n";
			else
				fingerprintEntry += "value\n";
			fingerprintEntry += macro.value;
			fingerprintEntries.push_back(std::move(fingerprintEntry));
			propertyMacros.push_back(std::move(macro));
		}

		if (!fingerprintEntries.empty())
		{
			std::sort(fingerprintEntries.begin(), fingerprintEntries.end());
			std::string fingerprintInput;

			for (const std::string& entry : fingerprintEntries)
				fingerprintInput += entry + "\n";

			fingerprint = FormatHash(HashText(fingerprintInput));
		}
	}

	const std::vector<std::string>& CompileConfiguration::GetDefines() const
	{
		return defines;
	}

	const std::string& CompileConfiguration::GetFingerprint() const
	{
		return fingerprint;
	}

	std::string CompileConfiguration::GetSourceFingerprint(const std::string& shaderSourcePath) const
	{
		const std::string cacheKey = NormalizeLookupPath(shaderSourcePath, modifiedShadersDirectory);
		const auto cachedFingerprint = sourceFingerprints.find(cacheKey);

		if (cachedFingerprint != sourceFingerprints.end())
			return cachedFingerprint->second;

		return fingerprint;
	}

	void CompileConfiguration::PrepareSourceFingerprints(
		const std::vector<std::string>& shaderSourcePaths,
		std::shared_ptr<const CompileConfiguration> previousConfiguration)
	{
		if (previousConfiguration && fingerprint == previousConfiguration->fingerprint)
		{
			sourceFingerprints = previousConfiguration->sourceFingerprints;
			return;
		}

		using MacroEntriesByPath = std::unordered_map<std::string, std::vector<std::string>>;

		const auto buildMacroEntriesByPath = [](const CompileConfiguration& configuration)
		{
			MacroEntriesByPath entriesByPath;
			const std::filesystem::path modifiedShadersPath = std::filesystem::u8path(configuration.modifiedShadersDirectory);

			for (const PropertyMacro& macro : configuration.propertyMacros)
			{
				std::filesystem::path macroPath = std::filesystem::u8path(macro.sourcePath);

				if (!macroPath.is_absolute())
					macroPath = modifiedShadersPath / macroPath;

				const std::string canonicalMacroPath = NormalizeAbsoluteSourcePath(macroPath);
				std::string fingerprintEntry = canonicalMacroPath + "\n" + macro.id + "\n" + macro.alias + "\n" + macro.name + "\n" + std::to_string(macro.definitionIndex) + "\n";

				if (macro.usesDefinitionPresence)
					fingerprintEntry += "presence\n";
				else
					fingerprintEntry += "value\n";

				fingerprintEntry += macro.value;
				entriesByPath[canonicalMacroPath].push_back(std::move(fingerprintEntry));
			}

			return entriesByPath;
		};

		const auto fingerprintForDependencies = [](const std::set<std::string>& dependencyPaths, const MacroEntriesByPath& entriesByPath)
		{
			std::vector<std::string> fingerprintEntries;

			for (const std::string& dependencyPath : dependencyPaths)
			{
				const auto entries = entriesByPath.find(dependencyPath);

				if (entries == entriesByPath.end())
					continue;

				fingerprintEntries.insert(fingerprintEntries.end(), entries->second.begin(), entries->second.end());
			}

			if (fingerprintEntries.empty())
				return std::string();

			std::sort(fingerprintEntries.begin(), fingerprintEntries.end());
			std::string fingerprintInput;

			for (const std::string& entry : fingerprintEntries)
				fingerprintInput += entry + "\n";

			return FormatHash(HashText(fingerprintInput));
		};

		const std::string sharedIncludesDirectory = ShaderInjectorIO::GetModifiedShadersIncludesDirectory();
		const std::vector<ShaderSourceDependencyResult> dependencyResults = CollectShaderSourceDependencyBatch(
			shaderSourcePaths,
			modifiedShadersDirectory,
			sharedIncludesDirectory);
		const MacroEntriesByPath currentEntriesByPath = buildMacroEntriesByPath(*this);
		MacroEntriesByPath previousEntriesByPath;

		if (previousConfiguration)
			previousEntriesByPath = buildMacroEntriesByPath(*previousConfiguration);

		std::unordered_map<std::string, std::string> preparedFingerprints;
		preparedFingerprints.reserve(shaderSourcePaths.size());

		for (size_t sourceIndex = 0; sourceIndex < shaderSourcePaths.size(); ++sourceIndex)
		{
			const std::string& shaderSourcePath = shaderSourcePaths[sourceIndex];
			const ShaderSourceDependencyResult& dependencyResult = dependencyResults[sourceIndex];
			std::string sourceFingerprint = fingerprint;

			if (dependencyResult.resolved)
			{
				const std::string currentSemanticFingerprint = fingerprintForDependencies(dependencyResult.normalizedPaths, currentEntriesByPath);

				if (previousConfiguration)
				{
					const std::string previousSemanticFingerprint = fingerprintForDependencies(dependencyResult.normalizedPaths, previousEntriesByPath);

					if (currentSemanticFingerprint == previousSemanticFingerprint)
						sourceFingerprint = previousConfiguration->GetSourceFingerprint(shaderSourcePath);
					else
						sourceFingerprint = currentSemanticFingerprint;
				}
				else
					sourceFingerprint = currentSemanticFingerprint;
			}

			preparedFingerprints[NormalizeLookupPath(shaderSourcePath, modifiedShadersDirectory)] = std::move(sourceFingerprint);
		}

		sourceFingerprints = std::move(preparedFingerprints);
	}

	std::shared_ptr<ConfigurationProgress> CompileConfiguration::GetProgress() const
	{
		return progress;
	}

	bool CompileConfiguration::PrepareSource(
		const std::string& relativeSourcePath,
		const std::string& sourceText,
		std::string& outSourceText,
		std::string& outError) const
	{
		outError.clear();
		outSourceText.clear();
		const std::string normalizedSourcePath = NormalizeSourcePath(relativeSourcePath);
		std::unordered_map<std::string, const PropertyMacro*> propertyByDefinition;

		for (const PropertyMacro& macro : propertyMacros)
		{
			if (macro.sourcePath == normalizedSourcePath)
				propertyByDefinition[macro.name + "\n" + std::to_string(macro.definitionIndex)] = &macro;
		}

		if (propertyByDefinition.empty())
		{
			outSourceText = sourceText;
			return true;
		}

		std::unordered_map<std::string, int> definitionCounts;
		std::unordered_map<std::string, bool> rewrittenProperties;
		const std::vector<SourceLine> lines = SplitSourceLines(sourceText);
		std::string preparedSource;
		preparedSource.reserve(sourceText.size() + propertyByDefinition.size() * 80);
		size_t lineNumber = 0;

		for (const SourceLine& sourceLine : lines)
		{
			++lineNumber;
			ParsedDefine define{};

			if (TryParseDefine(sourceLine.content, define))
			{
				const int definitionIndex = definitionCounts[define.name]++;
				const std::string key = define.name + "\n" + std::to_string(definitionIndex);
				const auto property = propertyByDefinition.find(key);

				if (property != propertyByDefinition.end())
				{
					const PropertyMacro& macro = *property->second;
					const std::string prefix = SourcePrefix(sourceLine.content);
					const std::string indent = SourceIndent(sourceLine.content);
					rewrittenProperties[macro.id] = true;

					if (macro.usesDefinitionPresence)
					{
						std::string lineEnding = sourceLine.ending;

						if (lineEnding.empty())
							lineEnding = "\n";

						preparedSource += prefix + "#if " + macro.alias + lineEnding;
						preparedSource += indent + "#define " + macro.name + lineEnding;
						preparedSource += indent + "#else" + lineEnding;
						preparedSource += indent + "#undef " + macro.name + lineEnding;
						preparedSource += indent + "#endif" + lineEnding;
						preparedSource += indent + "#line " + std::to_string(lineNumber + 1) + lineEnding;
					}
					else
					{
						preparedSource += prefix + "#define " + macro.name + " " + macro.alias;
						preparedSource += sourceLine.ending;
					}
					continue;
				}
			}

			preparedSource += sourceLine.content;
			preparedSource += sourceLine.ending;
		}

		for (const PropertyMacro& macro : propertyMacros)
		{
			if (macro.sourcePath == normalizedSourcePath && rewrittenProperties.find(macro.id) == rewrittenProperties.end())
			{
				outError = "Could not locate #define " + macro.name + " in " + macro.sourcePath;
				return false;
			}
		}

		outSourceText = std::move(preparedSource);
		return true;
	}
} //namespace ShaderConfiguration
