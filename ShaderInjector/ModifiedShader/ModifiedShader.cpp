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

	const ModifiedShaderCompiledVariant* ModifiedShaderPackageDisk::FindVariantForDefines(const std::vector<ModifiedShader::ShaderDefine>& defines) const
	{
		for (const ModifiedShaderCompiledVariant& variant : compiledVariants)
		{
			if (variant.defines == defines)
				return &variant;
		}

		return nullptr;
	}

	bool ModifiedShaderPackageDisk::HasPerTargetDefines() const
	{
		for (const ModifiedShaderTargetDisk& target : targets)
		{
			if (!target.defines.empty())
				return true;
		}

		return false;
	}

	void ModifiedShaderPackageDisk::RebuildVariantIndex()
	{
		variantIndexByShaderHash.clear();
		defaultVariantIndex = SIZE_MAX;

		for (size_t variantIndex = 0; variantIndex < compiledVariants.size(); ++variantIndex)
		{
			if (compiledVariants[variantIndex].defines.empty())
				defaultVariantIndex = variantIndex;
		}

		for (const ModifiedShaderTargetDisk& target : targets)
		{
			const ModifiedShaderCompiledVariant* variant = FindVariantForDefines(target.defines);

			if (!variant)
				continue;

			const size_t variantIndex = static_cast<size_t>(variant - compiledVariants.data());

			for (const std::string& knownHash : target.knownShaderBytecodeHashes)
			{
				const uint64_t shaderHash = Hash::ParseHashText(knownHash);

				if (!shaderHash)
					continue;

				//a hash listed under two define sets would resolve to an arbitrary permutation, so keep the
				//first one and report the conflict instead of letting the map order decide.
				if (!variantIndexByShaderHash.emplace(shaderHash, variantIndex).second)
				{
					ShaderInjectorIO::WriteToLogFileWarning(
						"ModifiedShader->RebuildVariantIndex: shader " + Hash::FormatHash(shaderHash) +
						" is listed under more than one define set in " + id + "; using the first target");
				}
			}
		}
	}

	const ModifiedShaderCompiledVariant* ModifiedShaderPackageDisk::FindVariantForShader(
		const std::string& originalShaderBytecodeHash,
		const std::vector<std::string>& shaderBytecodeHashAliases) const
	{
		auto found = variantIndexByShaderHash.find(Hash::ParseHashText(originalShaderBytecodeHash));

		for (size_t aliasIndex = 0; found == variantIndexByShaderHash.end() && aliasIndex < shaderBytecodeHashAliases.size(); ++aliasIndex)
			found = variantIndexByShaderHash.find(Hash::ParseHashText(shaderBytecodeHashAliases[aliasIndex]));

		//in a package with per-target defines every variant targets one permutation, so a fingerprint match
		//would get an arbitrary permutation; only define-free packages fall back to the default variant.
		if (found == variantIndexByShaderHash.end() && (defaultVariantIndex >= compiledVariants.size() || HasPerTargetDefines()))
			return nullptr;

		const size_t variantIndex = found != variantIndexByShaderHash.end() ? found->second : defaultVariantIndex;

		if (variantIndex >= compiledVariants.size())
			return nullptr;

		return &compiledVariants[variantIndex];
	}

	bool WriteJson(const ModifiedShaderPackageDisk& package)
	{
		if (package.jsonPath.empty())
			return false;

		ModifiedShaderPackageDisk portablePackage = package;
		//store file names so packages can move without changing their saved paths.
		if (portablePackage.sourceFile.empty())
			portablePackage.sourceFile = portablePackage.sourcePath;
		portablePackage.sourceFile = ShaderInjectorIO::FileNameFromPath(portablePackage.sourceFile);
		if (portablePackage.compiledBlobFile.empty())
			portablePackage.compiledBlobFile = portablePackage.compiledBlobPath;
		portablePackage.compiledBlobFile = ShaderInjectorIO::FileNameFromPath(portablePackage.compiledBlobFile);
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
			package.sourcePath.clear();
			if (!package.sourceFile.empty())
				package.sourcePath = ShaderInjectorIO::JoinPath(package.packageDirectory, package.sourceFile);
			package.compiledBlobFile = ShaderInjectorIO::FileNameFromPath(package.compiledBlobFile);
			package.compiledBlobPath.clear();
			if (!package.compiledBlobFile.empty())
				package.compiledBlobPath = ShaderInjectorIO::JoinPath(package.packageDirectory, package.compiledBlobFile);

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
} //namespace ModifiedShader
