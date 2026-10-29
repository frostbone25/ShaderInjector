#include "DatabaseModifiedShaders.h"
#include "DatabaseModifiedShadersInternal.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>
#include <utility>

#include <d3d12shader.h>

#include "GUI/ShaderInjectorGUI.h"
#include "Hash/Hash.h"
#include "IO/ShaderInjectorIO.h"
#include "ShaderAnalysis/ShaderAnalyzer.h"
#include "StringHelper.h"

namespace DatabaseModifiedShaders
{
	namespace Detail
	{
		static bool SemanticNamesEqual(const std::string& left, const std::string& right)
		{
			return left.size() == right.size() && std::equal(
													  left.begin(),
													  left.end(),
													  right.begin(),
													  [](unsigned char leftCharacter, unsigned char rightCharacter)
													  {
														  return std::tolower(leftCharacter) == std::tolower(rightCharacter);
													  });
		}

		static bool SignatureLayoutsMatch(
			const std::vector<ShaderAnalysis::SignatureParameterDisk>& expected,
			const std::vector<ShaderAnalysis::SignatureParameterDisk>& candidate)
		{
			if (expected.size() != candidate.size())
				return false;

			//match parameters by semantic name/index, then require the same register assignment and component layout.
			//declaration order may differ.
			for (const ShaderAnalysis::SignatureParameterDisk& expectedParameter : expected)
			{
				const auto candidateParameter = std::find_if(
					candidate.begin(),
					candidate.end(),
					[&](const ShaderAnalysis::SignatureParameterDisk& parameter)
					{
						return SemanticNamesEqual(parameter.semanticName, expectedParameter.semanticName) &&
							   parameter.semanticIndex == expectedParameter.semanticIndex;
					});

				if (candidateParameter == candidate.end() ||
					candidateParameter->registerIndex != expectedParameter.registerIndex ||
					candidateParameter->systemValueType != expectedParameter.systemValueType ||
					candidateParameter->componentType != expectedParameter.componentType ||
					candidateParameter->mask != expectedParameter.mask ||
					candidateParameter->stream != expectedParameter.stream ||
					candidateParameter->minimumPrecision != expectedParameter.minimumPrecision)
				{
					return false;
				}
			}

			return true;
		}

		bool ShaderInterfaceLayoutsMatch(
			const ShaderAnalysis::ShaderAnalysisDisk& expected,
			const ShaderAnalysis::ShaderAnalysisDisk& candidate)
		{
			if (expected.shaderStage == D3D12_SHVER_MESH_SHADER &&
				expected.executionProperties.geometryOutputTopology != 0 &&
				expected.executionProperties.geometryOutputTopology != candidate.executionProperties.geometryOutputTopology)
				return false;

			return expected.succeeded && candidate.succeeded &&
				   expected.shaderStage == candidate.shaderStage &&
				   SignatureLayoutsMatch(expected.inputParameters, candidate.inputParameters) &&
				   SignatureLayoutsMatch(expected.outputParameters, candidate.outputParameters) &&
				   SignatureLayoutsMatch(expected.patchConstantParameters, candidate.patchConstantParameters);
		}

		//a define filter limits the comparison to the targets that share one variant's define set.
		static bool TargetMatchesDefineFilter(const ModifiedShader::ModifiedShaderTargetDisk& target, const std::vector<ModifiedShader::ShaderDefine>* defineFilter)
		{
			return !defineFilter || target.defines == *defineFilter;
		}

		static bool PackageHasAnalyzedTargetInterface(const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader, const std::vector<ModifiedShader::ShaderDefine>* defineFilter)
		{
			return std::any_of(
				modifiedShader.targets.begin(),
				modifiedShader.targets.end(),
				[&](const ModifiedShader::ModifiedShaderTargetDisk& target)
				{
					return TargetMatchesDefineFilter(target, defineFilter) && target.shaderAnalysis.succeeded;
				});
		}

		bool ShaderInterfaceMatchesAnyPackageTarget(
			const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
			const ShaderAnalysis::ShaderAnalysisDisk& candidateAnalysis,
			const std::vector<ModifiedShader::ShaderDefine>* defineFilter)
		{
			if (!PackageHasAnalyzedTargetInterface(modifiedShader, defineFilter))
				return true;

			return std::any_of(
				modifiedShader.targets.begin(),
				modifiedShader.targets.end(),
				[&](const ModifiedShader::ModifiedShaderTargetDisk& target)
				{
					return TargetMatchesDefineFilter(target, defineFilter) && ShaderInterfaceLayoutsMatch(target.shaderAnalysis, candidateAnalysis);
				});
		}

		//the compiler layer takes "NAME=VALUE" strings so it does not need the package types.
		std::string FormatDefine(const ModifiedShader::ShaderDefine& define)
		{
			return define.name + "=" + define.value;
		}

		std::string DescribeDefines(const std::vector<ModifiedShader::ShaderDefine>& defines)
		{
			std::string description;

			for (const ModifiedShader::ShaderDefine& define : defines)
			{
				if (!description.empty())
					description += ";";

				description += FormatDefine(define);
			}

			return description;
		}

		static std::vector<std::string> CompilerDefineStrings(const std::vector<ModifiedShader::ShaderDefine>& defines)
		{
			std::vector<std::string> defineStrings;
			defineStrings.reserve(defines.size());

			for (const ModifiedShader::ShaderDefine& define : defines)
				defineStrings.push_back(FormatDefine(define));

			return defineStrings;
		}

		static std::string DescribeSignature(const ShaderAnalysis::ShaderAnalysisDisk& analysis, const std::vector<ShaderAnalysis::SignatureParameterDisk>& parameters)
		{
			if (!analysis.succeeded)
				return "unavailable";

			std::string description;

			for (const ShaderAnalysis::SignatureParameterDisk& parameter : parameters)
			{
				if (!description.empty())
					description += ",";

				description += parameter.semanticName + std::to_string(parameter.semanticIndex) + "@r" + std::to_string(parameter.registerIndex);
			}

			if (!description.empty())
				return description;

			return "none";
		}

		static std::string DescribeAnalysisError(const ShaderAnalysis::ShaderAnalysisDisk& analysis)
		{
			if (!analysis.error.empty())
				return analysis.error;

			return "none";
		}

		bool AnalyzeCompiledBlob(
			const std::vector<uint8_t>& compiledBlob,
			ShaderAnalysis::ShaderAnalysisDisk& outAnalysis)
		{
			if (compiledBlob.empty())
				return false;

			//signature validation only needs structured reflection.
			//an empty portable identity filter prevents the expensive disassembly analysis here.
			static const std::unordered_set<std::string> noDisassemblyCandidates;

			return ShaderAnalyzer::Analyze(
				compiledBlob.data(),
				compiledBlob.size(),
				outAnalysis,
				&noDisassemblyCandidates);
		}

		static bool CompileWithoutAnalyzedInterface(
			const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
			const std::vector<ModifiedShader::ShaderDefine>& defines,
			const std::string& candidateBasePath,
			std::vector<uint8_t>& outBlob,
			ShaderAnalysis::ShaderAnalysisDisk& outAnalysis)
		{
			std::string compiledBlobPath = candidateBasePath + ".candidate";

			if (!ShaderInjectorIO::CompileSourceToDXILBlob(
					modifiedShader.sourcePath,
					modifiedShader.shaderProfile,
					modifiedShader.shaderEntryPoint,
					compiledBlobPath,
					ShaderInjectorIO::ShaderSignaturePacking::PrefixStable,
					CompilerDefineStrings(defines),
					modifiedShader.compilerArguments))
			{
				ShaderInjectorIO::DeleteFileIfExists(compiledBlobPath);
				return false;
			}

			outBlob.clear();
			outAnalysis = {};

			const bool loaded = ShaderInjectorIO::LoadDXILBlobFromDisk(compiledBlobPath, outBlob) && !outBlob.empty();
			ShaderInjectorIO::DeleteFileIfExists(compiledBlobPath);

			if (!loaded)
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseModifiedShaders->CompileVariant: compiled shader blob could not be loaded: " + compiledBlobPath);
				return false;
			}

			AnalyzeCompiledBlob(outBlob, outAnalysis);
			return true;
		}

		static bool CompileAndAnalyzeCandidate(
			const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
			const std::vector<ModifiedShader::ShaderDefine>& defines,
			const std::string& candidatePath,
			ShaderInjectorIO::ShaderSignaturePacking signaturePacking,
			std::vector<uint8_t>& outBlob,
			ShaderAnalysis::ShaderAnalysisDisk& outAnalysis)
		{
			std::string outputPath = candidatePath;
			return ShaderInjectorIO::CompileSourceToDXILBlob(
					   modifiedShader.sourcePath,
					   modifiedShader.shaderProfile,
					   modifiedShader.shaderEntryPoint,
					   outputPath,
					   signaturePacking,
					   CompilerDefineStrings(defines),
					   modifiedShader.compilerArguments) &&
				   ShaderInjectorIO::LoadDXILBlobFromDisk(outputPath, outBlob) &&
				   AnalyzeCompiledBlob(outBlob, outAnalysis);
		}

		bool CompileVariant(
			const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
			const std::vector<ModifiedShader::ShaderDefine>& defines,
			const std::string& candidateBasePath,
			std::vector<uint8_t>& outBlob,
			ShaderAnalysis::ShaderAnalysisDisk& outAnalysis)
		{
			//DXBC packages and older fingerprints may lack analyzable interface data.
			//compile them without rejecting an otherwise valid shader.
			if (!PackageHasAnalyzedTargetInterface(modifiedShader, &defines))
				return CompileWithoutAnalyzedInterface(modifiedShader, defines, candidateBasePath, outBlob, outAnalysis);

			const std::string prefixStableCandidatePath = candidateBasePath + ".prefix-stable-candidate";
			const std::string optimizedCandidatePath = candidateBasePath + ".optimized-candidate";
			ShaderInjectorIO::DeleteFileIfExists(prefixStableCandidatePath);
			ShaderInjectorIO::DeleteFileIfExists(optimizedCandidatePath);

			std::vector<uint8_t> prefixStableBlob;
			ShaderAnalysis::ShaderAnalysisDisk prefixStableAnalysis;

			const bool prefixStableCompiled = CompileAndAnalyzeCandidate(
				modifiedShader,
				defines,
				prefixStableCandidatePath,
				ShaderInjectorIO::ShaderSignaturePacking::PrefixStable,
				prefixStableBlob,
				prefixStableAnalysis);

			const bool prefixStableMatches = prefixStableCompiled && ShaderInterfaceMatchesAnyPackageTarget(modifiedShader, prefixStableAnalysis, &defines);

			std::vector<uint8_t> optimizedBlob;
			ShaderAnalysis::ShaderAnalysisDisk optimizedAnalysis;
			bool optimizedCompiled = false;
			bool optimizedMatches = false;

			if (!prefixStableMatches && PackageHasAnalyzedTargetInterface(modifiedShader, &defines))
			{
				//a second packing mode is only needed when the first compiled candidate does not match an interface captured from the original game shader.
				optimizedCompiled = CompileAndAnalyzeCandidate(modifiedShader, defines, optimizedCandidatePath, ShaderInjectorIO::ShaderSignaturePacking::Optimized, optimizedBlob, optimizedAnalysis);
				optimizedMatches = optimizedCompiled && ShaderInterfaceMatchesAnyPackageTarget(modifiedShader, optimizedAnalysis, &defines);
			}

			const std::vector<uint8_t>* selectedBlob = nullptr;
			const ShaderAnalysis::ShaderAnalysisDisk* selectedAnalysis = nullptr;

			if (prefixStableMatches)
			{
				selectedBlob = &prefixStableBlob;
				selectedAnalysis = &prefixStableAnalysis;
			}
			else if (optimizedMatches)
			{
				selectedBlob = &optimizedBlob;
				selectedAnalysis = &optimizedAnalysis;
				ShaderInjectorIO::WriteToLogFile("DatabaseModifiedShaders->CompileVariant: selected optimized signature packing for " + modifiedShader.id + " defines=" + DescribeDefines(defines));
			}

			if (!selectedBlob || !selectedAnalysis || selectedBlob->empty())
			{
				std::string expectedInput = "unavailable";
				std::string expectedOutput = "unavailable";
				std::string expectedPrimitiveOutput = "unavailable";

				for (const ModifiedShader::ModifiedShaderTargetDisk& target : modifiedShader.targets)
				{
					if (target.defines == defines && target.shaderAnalysis.succeeded)
					{
						expectedInput = DescribeSignature(target.shaderAnalysis, target.shaderAnalysis.inputParameters);
						expectedOutput = DescribeSignature(target.shaderAnalysis, target.shaderAnalysis.outputParameters);
						expectedPrimitiveOutput = DescribeSignature(target.shaderAnalysis, target.shaderAnalysis.patchConstantParameters);
						break;
					}
				}

				std::string failureReason = "DXC did not produce a candidate that could be compiled and reflected.";

				if (prefixStableCompiled || optimizedCompiled)
					failureReason = "DXC produced shader bytecode, but no candidate has an interface compatible with the original game shader.";

				ShaderInjectorGUI::WriteToRuntimeLogError(
					"DatabaseModifiedShaders->CompileVariant: " + failureReason +
					" The previous compiled blob was preserved. modifiedShader=" + modifiedShader.id +
					" defines=" + DescribeDefines(defines) +
					" source=" + modifiedShader.sourcePath +
					" expectedInput=" + expectedInput +
					" expectedOutput=" + expectedOutput +
					" expectedPrimitiveOutput=" + expectedPrimitiveOutput +
					" prefixStableInput=" + DescribeSignature(prefixStableAnalysis, prefixStableAnalysis.inputParameters) +
					" prefixStableOutput=" + DescribeSignature(prefixStableAnalysis, prefixStableAnalysis.outputParameters) +
					" prefixStablePrimitiveOutput=" + DescribeSignature(prefixStableAnalysis, prefixStableAnalysis.patchConstantParameters) +
					" optimizedInput=" + DescribeSignature(optimizedAnalysis, optimizedAnalysis.inputParameters) +
					" optimizedOutput=" + DescribeSignature(optimizedAnalysis, optimizedAnalysis.outputParameters) +
					" optimizedPrimitiveOutput=" + DescribeSignature(optimizedAnalysis, optimizedAnalysis.patchConstantParameters) +
					" prefixStableAnalysisError=" + DescribeAnalysisError(prefixStableAnalysis) +
					" optimizedAnalysisError=" + DescribeAnalysisError(optimizedAnalysis) +
					". Check the entry-point input, vertex-output, and primitive-output semantics against the captured interface.");

				ShaderInjectorIO::DeleteFileIfExists(prefixStableCandidatePath);
				ShaderInjectorIO::DeleteFileIfExists(optimizedCandidatePath);
				return false;
			}

			ShaderInjectorIO::DeleteFileIfExists(prefixStableCandidatePath);
			ShaderInjectorIO::DeleteFileIfExists(optimizedCandidatePath);

			outBlob = *selectedBlob;
			outAnalysis = *selectedAnalysis;
			return true;
		}

		//the variant without defines keeps the package blob path; others use
		//<compiled blob path without extension>.<FNV-1a of "NAME=VALUE;NAME=VALUE...">.blob
		std::string VariantBlobPath(const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader, const std::vector<ModifiedShader::ShaderDefine>& defines)
		{
			if (defines.empty())
				return modifiedShader.compiledBlobPath;

			const std::string description = DescribeDefines(defines);
			const std::string defineHash = Hash::FormatHash(Hash::HashMemory(description.data(), description.size()));
			std::string basePath = modifiedShader.compiledBlobPath;
			const size_t extension = basePath.find_last_of('.');
			const size_t separator = basePath.find_last_of("/\\");

			if (extension != std::string::npos && (separator == std::string::npos || extension > separator))
				basePath.erase(extension);

			return basePath + "." + defineHash + ShaderInjectorIO::extensionBLOB;
		}

		//keep the package-level blob populated so existing "is this package compiled" checks keep working.
		void UpdatePackageCompiledState(ModifiedShader::ModifiedShaderPackageDisk& modifiedShader)
		{
			modifiedShader.compiledBlob.clear();
			modifiedShader.compiledShaderAnalysis = {};
			modifiedShader.compiledShaderInterfaceCompatible = true;

			for (const ModifiedShader::ModifiedShaderCompiledVariant& variant : modifiedShader.compiledVariants)
			{
				if (variant.compiledBlob.empty() || !variant.compiledShaderInterfaceCompatible)
					continue;

				modifiedShader.compiledBlob = variant.compiledBlob;
				modifiedShader.compiledShaderAnalysis = variant.compiledShaderAnalysis;
				return;
			}
		}

		//sort by define name then value so variant keys, blob file names, and compare-equal collapses do not
		//depend on JSON attribute order. argument order is irrelevant for the used macro forms.
		static void SortTargetDefines(ModifiedShader::ModifiedShaderTargetDisk& target)
		{
			std::sort(target.defines.begin(), target.defines.end(), [](const ModifiedShader::ShaderDefine& left, const ModifiedShader::ShaderDefine& right)
			{
				if (left.name != right.name)
					return left.name < right.name;

				return left.value < right.value;
			});
		}

		//one variant per distinct define set, in target order; packages without defines get one variant.
		//recapture rebuilds the list after adding targets and keeps the blobs it already loaded in memory.
		void LoadPackageVariants(ModifiedShader::ModifiedShaderPackageDisk& modifiedShader, bool preserveLoadedBlobs)
		{
			std::vector<ModifiedShader::ModifiedShaderCompiledVariant> variants;

			for (ModifiedShader::ModifiedShaderTargetDisk& target : modifiedShader.targets)
			{
				SortTargetDefines(target);

				const bool alreadyListed = std::any_of(variants.begin(), variants.end(), [&](const ModifiedShader::ModifiedShaderCompiledVariant& variant)
				{
					return variant.defines == target.defines;
				});

				if (alreadyListed)
					continue;

				ModifiedShader::ModifiedShaderCompiledVariant variant;
				variant.defines = target.defines;

				if (preserveLoadedBlobs)
				{
					const ModifiedShader::ModifiedShaderCompiledVariant* loadedVariant = modifiedShader.FindVariantForDefines(variant.defines);

					if (loadedVariant)
					{
						//recapture keeps the compiled bytecode exactly as it is; only the compatibility
						//flag is re-derived because recapture can add targets.
						variant.compiledBlob = loadedVariant->compiledBlob;
						variant.compiledShaderAnalysis = loadedVariant->compiledShaderAnalysis;
						variant.compiledShaderInterfaceCompatible = loadedVariant->compiledBlob.empty() ||
							ShaderInterfaceMatchesAnyPackageTarget(modifiedShader, loadedVariant->compiledShaderAnalysis, &variant.defines);
						variant.compileFailed = loadedVariant->compileFailed;
						variant.pendingCompileSerial = loadedVariant->pendingCompileSerial;
					}
				}

				variants.push_back(std::move(variant));
			}

			if (variants.empty())
				variants.emplace_back();

			for (ModifiedShader::ModifiedShaderCompiledVariant& variant : variants)
			{
				variant.compiledBlobPath = VariantBlobPath(modifiedShader, variant.defines);

				if (!variant.compiledBlob.empty())
					continue;

				if (!ShaderInjectorIO::FileExists(variant.compiledBlobPath) ||
					!ShaderInjectorIO::LoadDXILBlobFromDisk(variant.compiledBlobPath, variant.compiledBlob) ||
					variant.compiledBlob.empty())
				{
					variant.compiledBlob.clear();
					continue;
				}

				AnalyzeCompiledBlob(variant.compiledBlob, variant.compiledShaderAnalysis);
				variant.compiledShaderInterfaceCompatible = ShaderInterfaceMatchesAnyPackageTarget(modifiedShader, variant.compiledShaderAnalysis, &variant.defines);

				if (!variant.compiledShaderInterfaceCompatible)
				{
					ShaderInjectorIO::WriteToLogFileWarning("DatabaseModifiedShaders->LoadPackageVariants: incompatible compiled interface, will recompile " + modifiedShader.id + " defines=" + DescribeDefines(variant.defines));
					variant.compiledBlob.clear();
					variant.compiledShaderAnalysis = {};
				}
			}

			//lookups from other threads read the variant list under gCompileMutex.
			std::lock_guard<std::mutex> lock(gCompileMutex);
			modifiedShader.compiledVariants = std::move(variants);
			modifiedShader.RebuildVariantIndex();
			UpdatePackageCompiledState(modifiedShader);
		}
	} //namespace Detail

	bool CompileModifiedShader(const std::string& modifiedShaderId)
	{
		ModifiedShader::ModifiedShaderPackageDisk* modifiedShader = Detail::FindMutableModifiedShaderById(modifiedShaderId);

		if (!modifiedShader)
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseModifiedShaders->CompileModifiedShader: package was not found: " + modifiedShaderId);
			return false;
		}

		if (modifiedShader->sourcePath.empty())
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseModifiedShaders->CompileModifiedShader: package has no source path: " + modifiedShaderId);
			return false;
		}

		if (modifiedShader->compiledBlobPath.empty())
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseModifiedShaders->CompileModifiedShader: package has no compiled blob path: " + modifiedShaderId);
			return false;
		}

		modifiedShader->shaderProfile = StringHelper::ShaderProfileForType(modifiedShader->shaderType);

		if (modifiedShader->shaderProfile.empty())
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseModifiedShaders->CompileModifiedShader: no shader profile is configured for " + modifiedShaderId);
			return false;
		}

		return Detail::QueuePackageCompiles(*modifiedShader) > 0;
	}

	static bool ReadyVariantForShader(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		const std::string& originalShaderBytecodeHash,
		const std::vector<std::string>& shaderBytecodeHashAliases,
		std::vector<uint8_t>* outBlob,
		ShaderAnalysis::ShaderAnalysisDisk* outAnalysis)
	{
		bool needsCompile = false;
		bool ready = false;
		std::vector<ModifiedShader::ShaderDefine> defines;

		{
			std::lock_guard<std::mutex> lock(Detail::gCompileMutex);

			const ModifiedShader::ModifiedShaderCompiledVariant* variant = modifiedShader.FindVariantForShader(originalShaderBytecodeHash, shaderBytecodeHashAliases);

			if (!variant)
				return false;

			if (!variant->compiledBlob.empty() && variant->compiledShaderInterfaceCompatible)
			{
				ready = true;

				if (outBlob)
					*outBlob = variant->compiledBlob;

				if (outAnalysis)
					*outAnalysis = variant->compiledShaderAnalysis;
			}
			else
			{
				needsCompile = variant->compiledBlob.empty() && !variant->compileFailed && variant->pendingCompileSerial == 0;
				defines = variant->defines;
			}
		}

		if (needsCompile)
			RequestVariantCompile(modifiedShader.id, defines);

		return ready;
	}

	bool CompiledShaderMatchesTargetInterface(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		const ShaderAnalysis::ShaderAnalysisDisk& targetAnalysis,
		const std::string& originalShaderBytecodeHash,
		const std::vector<std::string>& shaderBytecodeHashAliases)
	{
		ShaderAnalysis::ShaderAnalysisDisk analysis;

		if (!ReadyVariantForShader(modifiedShader, originalShaderBytecodeHash, shaderBytecodeHashAliases, nullptr, &analysis))
			return false;

		if (!targetAnalysis.succeeded || !analysis.succeeded)
			return true;

		return Detail::ShaderInterfaceLayoutsMatch(targetAnalysis, analysis);
	}

	bool TryGetCompiledBlobForShader(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		const std::string& originalShaderBytecodeHash,
		const std::vector<std::string>& shaderBytecodeHashAliases,
		std::vector<uint8_t>& outBlob)
	{
		return ReadyVariantForShader(modifiedShader, originalShaderBytecodeHash, shaderBytecodeHashAliases, &outBlob, nullptr);
	}

	bool TryGetVariantDefinesForShader(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		const std::string& originalShaderBytecodeHash,
		const std::vector<std::string>& shaderBytecodeHashAliases,
		std::vector<ModifiedShader::ShaderDefine>& outDefines)
	{
		std::lock_guard<std::mutex> lock(Detail::gCompileMutex);
		const ModifiedShader::ModifiedShaderCompiledVariant* variant = modifiedShader.FindVariantForShader(originalShaderBytecodeHash, shaderBytecodeHashAliases);

		if (!variant)
			return false;

		outDefines = variant->defines;
		return true;
	}

	void LoadSourceVariants(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		const std::string& sourcePath,
		const std::string& compiledBlobPath,
		std::vector<ModifiedShader::ModifiedShaderCompiledVariant>& outVariants)
	{
		ModifiedShader::ModifiedShaderPackageDisk sourcePackage = modifiedShader;
		sourcePackage.sourcePath = sourcePath;
		sourcePackage.compiledBlobPath = compiledBlobPath;
		Detail::LoadPackageVariants(sourcePackage);
		outVariants = std::move(sourcePackage.compiledVariants);
	}

	bool HasCompiledVariant(const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader)
	{
		if (!modifiedShader.compiledBlob.empty())
			return true;

		for (const ModifiedShader::ModifiedShaderCompiledVariant& variant : modifiedShader.compiledVariants)
		{
			if (!variant.compiledBlob.empty() && variant.compiledShaderInterfaceCompatible)
				return true;
		}

		return false;
	}
} //namespace DatabaseModifiedShaders
