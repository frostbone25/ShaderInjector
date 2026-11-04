#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace ShaderConfiguration
{
	struct ShaderSourceDependencyResult
	{
		bool resolved = false;
		std::set<std::string> normalizedPaths;
	};

	struct ShaderSourceDependencyScanStats
	{
		size_t sourceFilesRead = 0;
	};

	//collect closures for a root batch while reusing parsed direct includes across roots
	std::vector<ShaderSourceDependencyResult> CollectShaderSourceDependencyBatch(
		const std::vector<std::string>& shaderSourcePaths,
		const std::string& modifiedShadersDirectory,
		const std::string& sharedIncludesDirectory,
		ShaderSourceDependencyScanStats* outStats = nullptr);
} //namespace ShaderConfiguration
