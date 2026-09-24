#include "DatabaseModifiedShaders.h"
#include "DatabaseModifiedShadersInternal.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>

#include "GUI/ShaderInjectorGUI.h"
#include "IO/ShaderInjectorIO.h"
#include "ShaderAnalyzer.h"
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

		static bool ShaderInterfaceLayoutsMatch(
			const ShaderAnalysis::ShaderAnalysisDisk& expected,
			const ShaderAnalysis::ShaderAnalysisDisk& candidate)
		{
			return expected.succeeded && candidate.succeeded &&
				   expected.shaderStage == candidate.shaderStage &&
				   SignatureLayoutsMatch(expected.inputParameters, candidate.inputParameters) &&
				   SignatureLayoutsMatch(expected.outputParameters, candidate.outputParameters) &&
				   SignatureLayoutsMatch(expected.patchConstantParameters, candidate.patchConstantParameters);
		}

		static bool PackageHasAnalyzedTargetInterface(const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader)
		{
			return std::any_of(
				modifiedShader.targets.begin(),
				modifiedShader.targets.end(),
				[](const ModifiedShader::ModifiedShaderTargetDisk& target)
				{
					return target.shaderAnalysis.succeeded;
				});
		}

		bool ShaderInterfaceMatchesAnyPackageTarget(
			const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
			const ShaderAnalysis::ShaderAnalysisDisk& candidateAnalysis)
		{
			if (!PackageHasAnalyzedTargetInterface(modifiedShader))
				return true;

			return std::any_of(
				modifiedShader.targets.begin(),
				modifiedShader.targets.end(),
				[&](const ModifiedShader::ModifiedShaderTargetDisk& target)
				{
					return ShaderInterfaceLayoutsMatch(target.shaderAnalysis, candidateAnalysis);
				});
		}

		static std::string DescribeInputSignature(const ShaderAnalysis::ShaderAnalysisDisk& analysis)
		{
			if (!analysis.succeeded)
				return "unavailable";

			std::string description;

			for (const ShaderAnalysis::SignatureParameterDisk& parameter : analysis.inputParameters)
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

		static bool CompileWithoutAnalyzedInterface(ModifiedShader::ModifiedShaderPackageDisk& modifiedShader)
		{
			std::string compiledBlobPath = modifiedShader.compiledBlobPath;

			if (!ShaderInjectorIO::CompileSourceToDXILBlob(
					modifiedShader.sourcePath,
					modifiedShader.shaderProfile,
					modifiedShader.shaderEntryPoint,
					compiledBlobPath))
			{
				return false;
			}

			modifiedShader.compiledBlob.clear();
			modifiedShader.compiledShaderAnalysis = {};

			if (!ShaderInjectorIO::LoadDXILBlobFromDisk(
					compiledBlobPath,
					modifiedShader.compiledBlob) ||
				modifiedShader.compiledBlob.empty())
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseModifiedShaders->CompileModifiedShader: compiled shader blob could not be loaded: " + compiledBlobPath);
				return false;
			}

			AnalyzeCompiledBlob(modifiedShader.compiledBlob, modifiedShader.compiledShaderAnalysis);
			modifiedShader.compiledShaderInterfaceCompatible = true;

			if (!ModifiedShader::WriteJson(modifiedShader))
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseModifiedShaders->CompileModifiedShader: compiled successfully, but could not save package metadata: " + modifiedShader.jsonPath);
				return false;
			}

			return true;
		}

		static bool CompileAndAnalyzeCandidate(
			const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
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
					   signaturePacking) &&
				   ShaderInjectorIO::LoadDXILBlobFromDisk(outputPath, outBlob) &&
				   AnalyzeCompiledBlob(outBlob, outAnalysis);
		}

		bool CompileModifiedShaderPackage(ModifiedShader::ModifiedShaderPackageDisk& modifiedShader)
		{
			//DXBC packages and older fingerprints may lack analyzable interface data.
			//compile them without rejecting an otherwise valid shader.
			if (!PackageHasAnalyzedTargetInterface(modifiedShader))
				return CompileWithoutAnalyzedInterface(modifiedShader);

			const std::string prefixStableCandidatePath = modifiedShader.compiledBlobPath + ".prefix-stable-candidate";
			const std::string optimizedCandidatePath = modifiedShader.compiledBlobPath + ".optimized-candidate";
			ShaderInjectorIO::DeleteFileIfExists(prefixStableCandidatePath);
			ShaderInjectorIO::DeleteFileIfExists(optimizedCandidatePath);

			std::vector<uint8_t> prefixStableBlob;
			ShaderAnalysis::ShaderAnalysisDisk prefixStableAnalysis;

			const bool prefixStableCompiled = CompileAndAnalyzeCandidate(
				modifiedShader,
				prefixStableCandidatePath,
				ShaderInjectorIO::ShaderSignaturePacking::PrefixStable,
				prefixStableBlob,
				prefixStableAnalysis);

			const bool prefixStableMatches = prefixStableCompiled && ShaderInterfaceMatchesAnyPackageTarget(modifiedShader, prefixStableAnalysis);

			std::vector<uint8_t> optimizedBlob;
			ShaderAnalysis::ShaderAnalysisDisk optimizedAnalysis;
			bool optimizedCompiled = false;
			bool optimizedMatches = false;

			if (!prefixStableMatches && PackageHasAnalyzedTargetInterface(modifiedShader))
			{
				//a second packing mode is only needed when the first compiled candidate does not match an interface captured from the original game shader.
				optimizedCompiled = CompileAndAnalyzeCandidate(modifiedShader, optimizedCandidatePath, ShaderInjectorIO::ShaderSignaturePacking::Optimized, optimizedBlob, optimizedAnalysis);
				optimizedMatches = optimizedCompiled && ShaderInterfaceMatchesAnyPackageTarget(modifiedShader, optimizedAnalysis);
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
				ShaderInjectorIO::WriteToLogFile("DatabaseModifiedShaders->CompileModifiedShader: selected optimized signature packing for " + modifiedShader.id);
			}

			if (!selectedBlob || !selectedAnalysis || selectedBlob->empty())
			{
				std::string expectedSignature = "unavailable";

				for (const ModifiedShader::ModifiedShaderTargetDisk& target : modifiedShader.targets)
				{
					if (target.shaderAnalysis.succeeded)
					{
						expectedSignature = DescribeInputSignature(target.shaderAnalysis);
						break;
					}
				}

				std::string failureReason = "DXC did not produce a candidate that could be compiled and reflected.";
				if (prefixStableCompiled || optimizedCompiled)
					failureReason = "DXC produced shader bytecode, but no candidate has an interface compatible with the original game shader.";

				ShaderInjectorGUI::WriteToRuntimeLogError(
					"DatabaseModifiedShaders->CompileModifiedShader: " + failureReason +
					" The previous compiled blob was preserved. modifiedShader=" + modifiedShader.id +
					" source=" + modifiedShader.sourcePath +
					" expectedInput=" + expectedSignature +
					" prefixStableInput=" + DescribeInputSignature(prefixStableAnalysis) +
					" optimizedInput=" + DescribeInputSignature(optimizedAnalysis) +
					" prefixStableAnalysisError=" + DescribeAnalysisError(prefixStableAnalysis) +
					" optimizedAnalysisError=" + DescribeAnalysisError(optimizedAnalysis) +
					". Check the entry-point input semantics and declaration order against expectedInput.");

				ShaderInjectorIO::DeleteFileIfExists(prefixStableCandidatePath);
				ShaderInjectorIO::DeleteFileIfExists(optimizedCandidatePath);
				return false;
			}

			const bool wroteCompiledBlob = ShaderInjectorIO::WriteBinaryFile(modifiedShader.compiledBlobPath, selectedBlob->data(), selectedBlob->size());
			ShaderInjectorIO::DeleteFileIfExists(prefixStableCandidatePath);
			ShaderInjectorIO::DeleteFileIfExists(optimizedCandidatePath);

			if (!wroteCompiledBlob)
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseModifiedShaders->CompileModifiedShader: compatible shader compiled, but the blob could not be written: " + modifiedShader.compiledBlobPath);
				return false;
			}

			modifiedShader.compiledBlob = *selectedBlob;
			modifiedShader.compiledShaderAnalysis = *selectedAnalysis;
			modifiedShader.compiledShaderInterfaceCompatible = true;

			if (!ModifiedShader::WriteJson(modifiedShader))
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseModifiedShaders->CompileModifiedShader: compatible shader compiled, but package metadata could not be saved: " + modifiedShader.jsonPath);
				return false;
			}

			return true;
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

		const bool compiled = Detail::CompileModifiedShaderPackage(*modifiedShader);
		if (!compiled && !ModifiedShader::WriteJson(*modifiedShader))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("DatabaseModifiedShaders->CompileModifiedShader: could not save package metadata: " + modifiedShader->jsonPath);
		}

		return compiled;
	}

	bool CompiledShaderMatchesTargetInterface(
		const ModifiedShader::ModifiedShaderPackageDisk& modifiedShader,
		const ShaderAnalysis::ShaderAnalysisDisk& targetAnalysis)
	{
		//preserve the injector's existing missing-blob fallback behavior.
		//this check only rejects a blob that exists and is known to carry an incompatible stage interface.
		if (modifiedShader.compiledBlob.empty())
			return true;

		if (!targetAnalysis.succeeded)
			return modifiedShader.compiledShaderInterfaceCompatible;

		return modifiedShader.compiledShaderInterfaceCompatible &&
			   Detail::ShaderInterfaceLayoutsMatch(targetAnalysis, modifiedShader.compiledShaderAnalysis);
	}
} //namespace DatabaseModifiedShaders
