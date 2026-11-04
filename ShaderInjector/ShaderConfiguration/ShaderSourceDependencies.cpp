#include "ShaderConfiguration/ShaderSourceDependencies.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace ShaderConfiguration
{
	namespace
	{
		std::string NormalizePath(const std::filesystem::path& path)
		{
			std::string normalizedPath = path.lexically_normal().generic_u8string();

			for (char& character : normalizedPath)
			{
				if (character >= 'A' && character <= 'Z')
					character = static_cast<char>(character - 'A' + 'a');
			}

			return normalizedPath;
		}

		std::filesystem::path AbsoluteNormalizedPath(const std::filesystem::path& path)
		{
			std::error_code error;
			std::filesystem::path absolutePath = std::filesystem::absolute(path, error);

			if (error)
				return path.lexically_normal();

			std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(absolutePath, error);

			if (error)
				return absolutePath.lexically_normal();

			return canonicalPath.lexically_normal();
		}

		std::string Trim(const std::string& text)
		{
			const size_t first = text.find_first_not_of(" \t\r\n");

			if (first == std::string::npos)
				return {};

			const size_t last = text.find_last_not_of(" \t\r\n");
			return text.substr(first, last - first + 1);
		}

		std::string RemoveComments(const std::string& line, bool& insideBlockComment)
		{
			std::string result;
			bool insideString = false;
			bool escaped = false;

			for (size_t index = 0; index < line.size(); ++index)
			{
				const char character = line[index];
				char nextCharacter = '\0';

				if (index + 1 < line.size())
					nextCharacter = line[index + 1];

				if (insideBlockComment)
				{
					if (character == '*' && nextCharacter == '/')
					{
						insideBlockComment = false;
						++index;
					}

					continue;
				}

				if (!insideString && character == '/' && nextCharacter == '*')
				{
					result.push_back(' ');
					insideBlockComment = true;
					++index;
					continue;
				}

				if (!insideString && character == '/' && nextCharacter == '/')
					break;

				result.push_back(character);

				if (character == '"' && !escaped)
					insideString = !insideString;

				if (character == '\\' && !escaped)
					escaped = true;
				else
					escaped = false;
			}

			return result;
		}

		bool ReadIncludeName(const std::string& directive, std::string& outIncludeName)
		{
			std::string remaining = Trim(directive);

			if (remaining.empty() || remaining.front() != '#')
				return true;

			remaining = Trim(remaining.substr(1));
			size_t keywordEnd = 0;

			while (keywordEnd < remaining.size())
			{
				const unsigned char character = static_cast<unsigned char>(remaining[keywordEnd]);

				if (!std::isalnum(character) && character != '_')
					break;

				++keywordEnd;
			}

			const std::string keyword = remaining.substr(0, keywordEnd);

			if (keyword != "include")
				return true;

			remaining = Trim(remaining.substr(keywordEnd));

			if (remaining.size() < 2 || (remaining.front() != '"' && remaining.front() != '<'))
				return false;

			char closingCharacter = '>';

			if (remaining.front() == '"')
				closingCharacter = '"';

			const size_t closingIndex = remaining.find(closingCharacter, 1);

			if (closingIndex == std::string::npos)
				return false;

			if (!Trim(remaining.substr(closingIndex + 1)).empty())
				return false;

			outIncludeName = remaining.substr(1, closingIndex - 1);
			return !outIncludeName.empty();
		}

		bool ReadIncludes(const std::filesystem::path& sourcePath, std::vector<std::string>& outIncludeNames)
		{
			std::ifstream sourceFile(sourcePath, std::ios::binary);

			if (!sourceFile)
				return false;

			std::ostringstream sourceStream;
			sourceStream << sourceFile.rdbuf();
			std::string sourceText = sourceStream.str();

			if (sourceText.size() >= 3 &&
				static_cast<unsigned char>(sourceText[0]) == 0xEF &&
				static_cast<unsigned char>(sourceText[1]) == 0xBB &&
				static_cast<unsigned char>(sourceText[2]) == 0xBF)
				sourceText.erase(0, 3);

			std::istringstream sourceLines(sourceText);
			std::string line;
			std::string splicedSource;
			std::vector<std::string> logicalLines;

			while (std::getline(sourceLines, line))
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();

				const size_t lastNonWhitespace = line.find_last_not_of(" \t");
				const bool continued = lastNonWhitespace != std::string::npos && line[lastNonWhitespace] == '\\';

				if (continued)
					line.erase(lastNonWhitespace);

				splicedSource += line;

				if (!continued)
				{
					logicalLines.push_back(std::move(splicedSource));
					splicedSource.clear();
				}
			}

			if (!splicedSource.empty())
				logicalLines.push_back(std::move(splicedSource));

			bool insideBlockComment = false;

			for (const std::string& logicalSourceLine : logicalLines)
			{
				const std::string commentFreeLine = RemoveComments(logicalSourceLine, insideBlockComment);

				std::string includeName;
				if (!ReadIncludeName(commentFreeLine, includeName))
					return false;

				if (!includeName.empty())
					outIncludeNames.push_back(std::move(includeName));
			}

			return true;
		}

		struct ParsedShaderIncludes
		{
			bool succeeded = false;
			std::vector<std::string> includeNames;
		};

		struct ShaderSourceDependencyBatchCache
		{
			std::unordered_map<std::string, ParsedShaderIncludes> parsedIncludesByPath;
			std::unordered_map<std::string, std::string> resolvedIncludesByRequest;
			std::unordered_map<std::string, std::string> canonicalPathsByLexicalPath;
			ShaderSourceDependencyScanStats stats;
		};

		std::filesystem::path CanonicalizeCachedPath(
			const std::filesystem::path& path,
			ShaderSourceDependencyBatchCache& cache)
		{
			const std::filesystem::path absolutePath = std::filesystem::absolute(path).lexically_normal();
			const std::string lexicalKey = NormalizePath(absolutePath);
			const auto cachedPath = cache.canonicalPathsByLexicalPath.find(lexicalKey);

			if (cachedPath != cache.canonicalPathsByLexicalPath.end())
				return std::filesystem::u8path(cachedPath->second);

			const std::filesystem::path canonicalPath = AbsoluteNormalizedPath(absolutePath);
			cache.canonicalPathsByLexicalPath[lexicalKey] = canonicalPath.generic_u8string();
			return canonicalPath;
		}

		const ParsedShaderIncludes& ReadIncludesCached(
			const std::filesystem::path& sourcePath,
			ShaderSourceDependencyBatchCache& cache)
		{
			const std::filesystem::path canonicalPath = CanonicalizeCachedPath(sourcePath, cache);
			const std::string sourceKey = NormalizePath(canonicalPath);
			auto parsedIncludes = cache.parsedIncludesByPath.find(sourceKey);

			if (parsedIncludes != cache.parsedIncludesByPath.end())
				return parsedIncludes->second;

			ParsedShaderIncludes parsedSource{};
			++cache.stats.sourceFilesRead;
			parsedSource.succeeded = ReadIncludes(canonicalPath, parsedSource.includeNames);

			return cache.parsedIncludesByPath.emplace(sourceKey, std::move(parsedSource)).first->second;
		}

		bool ResolveInclude(
			const std::string& includeName,
			const std::filesystem::path& parentDirectory,
			const std::filesystem::path& sourceDirectory,
			const std::filesystem::path& sharedIncludesDirectory,
			std::filesystem::path& outIncludePath,
			ShaderSourceDependencyBatchCache& cache)
		{
			const std::string requestKey = NormalizePath(parentDirectory) + "\n" + NormalizePath(sourceDirectory) + "\n" + NormalizePath(sharedIncludesDirectory) + "\n" + includeName;
			const auto cachedRequest = cache.resolvedIncludesByRequest.find(requestKey);

			if (cachedRequest != cache.resolvedIncludesByRequest.end())
			{
				if (cachedRequest->second.empty())
					return false;

				outIncludePath = std::filesystem::u8path(cachedRequest->second);
				return true;
			}

			const std::filesystem::path includePath = std::filesystem::u8path(includeName);
			const std::filesystem::path candidates[] = {
				parentDirectory / includePath,
				sourceDirectory / includePath,
				sharedIncludesDirectory / includePath};

			for (const std::filesystem::path& candidate : candidates)
			{
				std::error_code error;
				if (std::filesystem::is_regular_file(candidate, error) && !error)
				{
					outIncludePath = CanonicalizeCachedPath(candidate, cache);
					cache.resolvedIncludesByRequest[requestKey] = outIncludePath.generic_u8string();
					return true;
				}
			}

			cache.resolvedIncludesByRequest[requestKey] = {};
			return false;
		}

		bool VisitSource(
			const std::filesystem::path& sourcePath,
			const std::filesystem::path& sourceDirectory,
			const std::filesystem::path& sharedIncludesDirectory,
			std::set<std::string>& visitedPaths,
			ShaderSourceDependencyBatchCache& cache)
		{
			const std::filesystem::path normalizedSourcePath = CanonicalizeCachedPath(sourcePath, cache);
			const std::string normalizedSourceKey = NormalizePath(normalizedSourcePath);

			if (!visitedPaths.insert(normalizedSourceKey).second)
				return true;

			const ParsedShaderIncludes& parsedIncludes = ReadIncludesCached(normalizedSourcePath, cache);
			if (!parsedIncludes.succeeded)
				return false;

			for (const std::string& includeName : parsedIncludes.includeNames)
			{
				std::filesystem::path includePath;
				if (!ResolveInclude(includeName, normalizedSourcePath.parent_path(), sourceDirectory, sharedIncludesDirectory, includePath, cache))
					return false;

				if (!VisitSource(includePath, sourceDirectory, sharedIncludesDirectory, visitedPaths, cache))
					return false;
			}

			return true;
		}
	}

	std::vector<ShaderSourceDependencyResult> CollectShaderSourceDependencyBatch(
		const std::vector<std::string>& shaderSourcePaths,
		const std::string& modifiedShadersDirectory,
		const std::string& sharedIncludesDirectory,
		ShaderSourceDependencyScanStats* outStats)
	{
		const std::filesystem::path modifiedShadersPath = AbsoluteNormalizedPath(std::filesystem::u8path(modifiedShadersDirectory));
		const std::filesystem::path sharedIncludesPath = AbsoluteNormalizedPath(std::filesystem::u8path(sharedIncludesDirectory));
		std::vector<ShaderSourceDependencyResult> results;
		results.reserve(shaderSourcePaths.size());
		ShaderSourceDependencyBatchCache cache;

		for (const std::string& shaderSourcePath : shaderSourcePaths)
		{
			std::filesystem::path shaderPath = std::filesystem::u8path(shaderSourcePath);

			if (!shaderPath.is_absolute())
				shaderPath = modifiedShadersPath / shaderPath;

			shaderPath = CanonicalizeCachedPath(shaderPath, cache);
			ShaderSourceDependencyResult result{};
			result.normalizedPaths.insert(NormalizePath(shaderPath));
			std::set<std::string> visitedPaths;
			result.resolved = VisitSource(shaderPath, shaderPath.parent_path(), sharedIncludesPath, visitedPaths, cache);

			if (!result.resolved)
				result.normalizedPaths.clear();
			else
				result.normalizedPaths = std::move(visitedPaths);

			results.push_back(std::move(result));
		}

		if (outStats)
			*outStats = cache.stats;

		return results;
	}
} //namespace ShaderConfiguration
