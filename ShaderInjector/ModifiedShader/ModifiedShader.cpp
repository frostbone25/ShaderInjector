#include "ModifiedShader.h"

#include <algorithm>

#include "Hash/Hash.h"
#include "IO/ShaderInjectorIO.h"

namespace ModifiedShader
{
	bool AnalysesHaveSameStrictIdentity(const ShaderAnalysis::ShaderAnalysisDisk& left, const ShaderAnalysis::ShaderAnalysisDisk& right)
	{
		return left.succeeded && right.succeeded && !left.crossVersionIdentityHash.empty() && left.crossVersionIdentityHash == right.crossVersionIdentityHash;
	}

	bool ModifiedShaderTargetDisk::MatchesShader(uint64_t shaderHash, const ShaderAnalysis::ShaderAnalysisDisk& analysis) const
	{
		if (shaderHash != 0)
		{
			for (const std::string& knownHash : knownShaderBytecodeHashes)
			{
				if (Hash::ParseHashText(knownHash) == shaderHash)
					return true;
			}
		}

		return AnalysesHaveSameStrictIdentity(shaderAnalysis, analysis);
	}

	bool ModifiedShaderPackageDisk::MatchesShader(uint64_t shaderHash, const ShaderAnalysis::ShaderAnalysisDisk& analysis) const
	{
		if (!enabled)
			return false;

		for (const ModifiedShaderTargetDisk& target : targets)
		{
			if (target.MatchesShader(shaderHash, analysis))
				return true;
		}

		return false;
	}

	bool WriteJson(const ModifiedShaderPackageDisk& package)
	{
		if (package.jsonPath.empty())
			return false;

		ModifiedShaderPackageDisk portablePackage = package;
		portablePackage.sourceFile = ShaderInjectorIO::FileNameFromPath(portablePackage.sourceFile.empty() ? portablePackage.sourcePath : portablePackage.sourceFile);
		portablePackage.compiledBlobFile = ShaderInjectorIO::FileNameFromPath(portablePackage.compiledBlobFile.empty() ? portablePackage.compiledBlobPath : portablePackage.compiledBlobFile);
		portablePackage.packageDirectory = ".";
		portablePackage.jsonPath = ShaderInjectorIO::FileNameFromPath(package.jsonPath);
		portablePackage.sourcePath = portablePackage.sourceFile;
		portablePackage.compiledBlobPath = portablePackage.compiledBlobFile;

		nlohmann::ordered_json json = portablePackage;
		json.erase("packageDirectory");
		json.erase("jsonPath");
		json.erase("sourcePath");
		json.erase("compiledBlobPath");
		return ShaderInjectorIO::WriteTextFile(package.jsonPath, json.dump(4));
	}

	bool LoadJson(const std::string& jsonPath, ModifiedShaderPackageDisk& outPackage)
	{
		try
		{
			std::string jsonText;

			if (!ShaderInjectorIO::ReadTextFile(jsonPath, jsonText))
				return false;

			const nlohmann::ordered_json json = nlohmann::ordered_json::parse(jsonText);
			ModifiedShaderPackageDisk package = json.get<ModifiedShaderPackageDisk>();

			package.jsonPath = jsonPath;
			package.packageDirectory = ShaderInjectorIO::DirectoryFromPath(jsonPath);
			package.sourceFile = ShaderInjectorIO::FileNameFromPath(package.sourceFile);
			package.sourcePath = package.sourceFile.empty() ? "" : ShaderInjectorIO::JoinPath(package.packageDirectory, package.sourceFile);
			package.compiledBlobFile = ShaderInjectorIO::FileNameFromPath(package.compiledBlobFile);
			package.compiledBlobPath = package.compiledBlobFile.empty() ? "" : ShaderInjectorIO::JoinPath(package.packageDirectory, package.compiledBlobFile);

			if (package.id.empty())
				package.id = ShaderInjectorIO::FileNameFromPath(package.packageDirectory);

			if (package.name.empty())
				package.name = package.id;

			outPackage = std::move(package);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	ModifiedShaderTargetDisk BuildTargetFromShaderTarget(
		const ShaderTarget::ShaderTargetDisk& shaderTarget,
		const std::string& targetApplication,
		const std::string& gameVersion)
	{
		ModifiedShaderTargetDisk target{};
		target.name = shaderTarget.name;
		target.targetApplication = targetApplication;
		target.gameVersion = gameVersion;
		target.originalShaderBytecodeLength = shaderTarget.originalShaderBytecodeLength;
		target.shaderAnalysis = shaderTarget.originalShaderAnalysis;

		auto addHash = [&target](const std::string& hash)
		{
			if (hash.empty() || std::find(target.knownShaderBytecodeHashes.begin(), target.knownShaderBytecodeHashes.end(), hash) != target.knownShaderBytecodeHashes.end())
				return;

			target.knownShaderBytecodeHashes.push_back(hash);
		};

		addHash(shaderTarget.originalShaderBytecodeHash);

		for (const std::string& aliasHash : shaderTarget.shaderBytecodeHashAliases)
			addHash(aliasHash);

		return target;
	}
}
