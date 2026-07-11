#pragma once

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <json.hpp>

#include "Hash.h"
#include "ModifiedShader.h"
#include "ShaderAnalysis.h"

namespace CrossVersionDiscoveryTest
{
	struct ShaderCapture
	{
		ShaderAnalysis::ShaderAnalysisDisk analysis;
		uint64_t rawHash = 0;
		size_t byteLength = 0;
	};

	inline std::filesystem::path DataRoot()
	{
		return std::filesystem::path(CROSS_VERSION_DISCOVERY_DATA_DIR);
	}

	inline ModifiedShader::PackageDisk LoadPackageJson(const std::filesystem::path& path)
	{
		std::ifstream input(path);
		if (!input)
			throw std::runtime_error("Could not open package fixture: " + path.string());

		nlohmann::ordered_json json{};
		input >> json;
		ModifiedShader::PackageDisk package = json.get<ModifiedShader::PackageDisk>();
		if (package.format != ModifiedShader::formatName)
			throw std::runtime_error("Unexpected package format in " + path.string());
		return package;
	}

	// Real ModifiedShader packages store original-game bytecode metadata in
	// targets[0] rather than a separate OriginalShaderBytecode.bin file.
	inline ShaderCapture LoadPackageTargetCapture(const std::filesystem::path& packageJsonPath)
	{
		const ModifiedShader::PackageDisk package = LoadPackageJson(packageJsonPath);
		if (package.targets.empty())
			throw std::runtime_error("Package has no targets: " + packageJsonPath.string());

		const ModifiedShader::TargetDisk& target = package.targets.front();
		if (target.knownShaderBytecodeHashes.empty())
			throw std::runtime_error("Package target has no knownShaderBytecodeHashes: " + packageJsonPath.string());
		if (target.originalShaderBytecodeLength.empty())
			throw std::runtime_error("Package target has no originalShaderBytecodeLength: " + packageJsonPath.string());

		ShaderCapture capture{};
		capture.analysis = target.shaderAnalysis;
		capture.rawHash = Hash::ParseHashText(target.knownShaderBytecodeHashes.front());
		capture.byteLength = static_cast<size_t>(std::stoull(target.originalShaderBytecodeLength));
		return capture;
	}
}
