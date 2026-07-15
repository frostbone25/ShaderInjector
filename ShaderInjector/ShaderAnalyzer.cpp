#include "ShaderAnalyzer.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <tuple>

#include <d3d12shader.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include "Hash.h"
#include "ShaderInjectorIO.h"
#include "StringHelper.h"

namespace ShaderAnalyzer
{
	namespace
	{
		using Microsoft::WRL::ComPtr;

		std::mutex gAnalyzerMutex;
		std::mutex gDXCompilerModuleMutex;
		HMODULE gDXCompilerModule = nullptr;

		bool Fail(ShaderAnalysis::ShaderAnalysisDisk& analysis, const std::string& error)
		{
			analysis.succeeded = false;
			analysis.error = error;
			return false;
		}

		std::string Fingerprint(const std::string& canonicalText)
		{
			return Hash::FormatHash(Hash::HashMemory(canonicalText.data(), canonicalText.size()));
		}

		std::string FourCCText(uint32_t kind)
		{
			std::string text(4, ' ');
			for (size_t index = 0; index < text.size(); ++index)
			{
				const char character = static_cast<char>((kind >> (index * 8)) & 0xff);
				text[index] = character >= 32 && character <= 126 ? character : '?';
			}
			return text;
		}

		std::pair<const char*, const char*> ShaderStageText(uint32_t shaderStage)
		{
			switch (shaderStage)
			{
				case D3D12_SHVER_PIXEL_SHADER: return { "PixelShader", "ps" };
				case D3D12_SHVER_VERTEX_SHADER: return { "VertexShader", "vs" };
				case D3D12_SHVER_GEOMETRY_SHADER: return { "GeometryShader", "gs" };
				case D3D12_SHVER_HULL_SHADER: return { "HullShader", "hs" };
				case D3D12_SHVER_DOMAIN_SHADER: return { "DomainShader", "ds" };
				case D3D12_SHVER_COMPUTE_SHADER: return { "ComputeShader", "cs" };
				case D3D12_SHVER_LIBRARY: return { "Library", "lib" };
				default: return { "Unknown", "unknown" };
			}
		}

		HMODULE LoadDXCompiler()
		{
			std::lock_guard<std::mutex> lock(gDXCompilerModuleMutex);

			if (gDXCompilerModule)
				return gDXCompilerModule;

			// Pin a copy already loaded by the game so another component cannot unload it
			// while the background analyzer is using DXC interfaces from that module.
			if (GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_PIN,
				L"dxcompiler.dll",
				&gDXCompilerModule))
			{
				return gDXCompilerModule;
			}

			const std::wstring configuredPath = StringHelper::Utf8ToWide(ShaderInjectorIO::GetToolPathDXCompiler());

			if (!configuredPath.empty())
			{
				gDXCompilerModule = LoadLibraryExW(
					configuredPath.c_str(),
					nullptr,
					LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
			}

			if (!gDXCompilerModule)
				gDXCompilerModule = LoadLibraryW(L"dxcompiler.dll");

			// Intentionally retain this module reference for the injector's lifetime.
			return gDXCompilerModule;
		}

		ShaderAnalysis::SignatureParameterDisk ReadSignatureParameter(const D3D12_SIGNATURE_PARAMETER_DESC& description)
		{
			ShaderAnalysis::SignatureParameterDisk parameter{};
			parameter.semanticName = StringHelper::SafeString(description.SemanticName);
			parameter.semanticIndex = description.SemanticIndex;
			parameter.registerIndex = description.Register;
			parameter.systemValueType = static_cast<uint32_t>(description.SystemValueType);
			parameter.componentType = static_cast<uint32_t>(description.ComponentType);
			parameter.mask = description.Mask;
			parameter.readWriteMask = description.ReadWriteMask;
			parameter.stream = description.Stream;
			parameter.minimumPrecision = static_cast<uint32_t>(description.MinPrecision);
			return parameter;
		}

		ShaderAnalysis::TypeLayoutDisk ReadTypeLayout(ID3D12ShaderReflectionType* reflectedType, uint32_t depth)
		{
			ShaderAnalysis::TypeLayoutDisk layout{};

			if (!reflectedType || depth >= 16)
				return layout;

			D3D12_SHADER_TYPE_DESC description{};

			if (FAILED(reflectedType->GetDesc(&description)))
				return layout;

			layout.name = StringHelper::SafeString(description.Name);
			layout.variableClass = static_cast<uint32_t>(description.Class);
			layout.variableType = static_cast<uint32_t>(description.Type);
			layout.rows = description.Rows;
			layout.columns = description.Columns;
			layout.elements = description.Elements;
			layout.offset = description.Offset;

			layout.members.reserve(description.Members);

			for (uint32_t memberIndex = 0; memberIndex < description.Members; ++memberIndex)
			{
				ShaderAnalysis::TypeLayoutDisk member = ReadTypeLayout(reflectedType->GetMemberTypeByIndex(memberIndex), depth + 1);
				const char* memberName = reflectedType->GetMemberTypeName(memberIndex);

				if (memberName)
					member.name = memberName;

				layout.members.push_back(std::move(member));
			}

			return layout;
		}

		void AppendTypeLayoutSignature(std::ostringstream& signature, const ShaderAnalysis::TypeLayoutDisk& layout)
		{
			signature << layout.variableClass << ':' << layout.variableType << ':' << layout.rows << ':' << layout.columns << ':' << layout.elements << ':' << layout.offset << '[';

			for (const ShaderAnalysis::TypeLayoutDisk& member : layout.members)
				AppendTypeLayoutSignature(signature, member);

			signature << ']';
		}

		std::string BuildSignatureParameterFingerprint(
			const std::vector<ShaderAnalysis::SignatureParameterDisk>& inputParameters,
			const std::vector<ShaderAnalysis::SignatureParameterDisk>& outputParameters,
			const std::vector<ShaderAnalysis::SignatureParameterDisk>& patchParameters)
		{
			auto appendParameters = [](std::ostringstream& signature, char category, std::vector<ShaderAnalysis::SignatureParameterDisk> parameters)
			{
				std::sort(parameters.begin(), parameters.end(), [](const auto& left, const auto& right)
				{
					return std::tie(left.stream, left.registerIndex, left.semanticName, left.semanticIndex) <
						std::tie(right.stream, right.registerIndex, right.semanticName, right.semanticIndex);
				});

				for (const auto& parameter : parameters)
				{
					signature << category << ':' << parameter.semanticName << ':' << parameter.semanticIndex << ':'
						<< parameter.registerIndex << ':' << parameter.systemValueType << ':' << parameter.componentType << ':'
						<< parameter.mask << ':' << parameter.readWriteMask << ':' << parameter.stream << ':'
						<< parameter.minimumPrecision << ';';
				}
			};

			std::ostringstream signature;
			appendParameters(signature, 'I', inputParameters);
			appendParameters(signature, 'O', outputParameters);
			appendParameters(signature, 'P', patchParameters);
			return Fingerprint(signature.str());
		}

		std::string BuildResourceFingerprint(std::vector<ShaderAnalysis::ResourceBindingDisk> resources)
		{
			std::sort(resources.begin(), resources.end(), [](const auto& left, const auto& right)
			{
				return std::tie(left.registerSpace, left.bindPoint, left.type, left.bindCount) < std::tie(right.registerSpace, right.bindPoint, right.type, right.bindCount);
			});

			std::ostringstream signature;

			for (const auto& resource : resources)
			{
				// Names and range IDs are intentionally excluded because compilers may rewrite them.
				signature << resource.type << ':' << resource.bindPoint << ':' << resource.bindCount << ':'
					<< resource.flags << ':' << resource.returnType << ':' << resource.dimension << ':'
					<< resource.sampleCountOrStride << ':' << resource.registerSpace << ';';
			}

			return Fingerprint(signature.str());
		}

		std::string BuildConstantBufferFingerprint(std::vector<ShaderAnalysis::ConstantBufferDisk> buffers)
		{
			std::sort(buffers.begin(), buffers.end(), [](const auto& left, const auto& right)
			{
				return std::make_tuple(left.type, left.size, left.variables.size()) <
					std::make_tuple(right.type, right.size, right.variables.size());
			});

			std::ostringstream signature;

			for (auto& buffer : buffers)
			{
				std::sort(buffer.variables.begin(), buffer.variables.end(), [](const auto& left, const auto& right)
				{
					return left.startOffset < right.startOffset;
				});

				signature << buffer.type << ':' << buffer.size << ':' << buffer.flags << '{';

				for (const auto& variable : buffer.variables)
				{
					signature << variable.startOffset << ':' << variable.size << ':' << variable.flags << ':'
						<< variable.startTexture << ':' << variable.textureCount << ':'
						<< variable.startSampler << ':' << variable.samplerCount << ':';
					AppendTypeLayoutSignature(signature, variable.typeLayout);
					signature << ';';
				}

				signature << '}';
			}

			return Fingerprint(signature.str());
		}

		std::string BuildInstructionFingerprint(const ShaderAnalysis::InstructionStatisticsDisk& statistics)
		{
			nlohmann::ordered_json json = statistics;
			return Fingerprint(json.dump());
		}

		std::string BuildExecutionFingerprint(const ShaderAnalysis::ExecutionPropertiesDisk& executionProperties)
		{
			nlohmann::ordered_json json = executionProperties;
			return Fingerprint(json.dump());
		}

		void AnalyzeSemanticInstructions(
			DxcCreateInstanceProc createInstance,
			IDxcBlob* shaderBlob,
			ShaderAnalysis::ShaderAnalysisDisk& analysis)
		{
			ComPtr<IDxcCompiler> compiler;

			if (FAILED(createInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))))
				return;

			ComPtr<IDxcBlobEncoding> disassemblyBlob;

			if (FAILED(compiler->Disassemble(shaderBlob, &disassemblyBlob)) || !disassemblyBlob)
				return;

			const char* disassemblyBytes = static_cast<const char*>(disassemblyBlob->GetBufferPointer());
			const size_t disassemblySize = disassemblyBlob->GetBufferSize();

			if (!disassemblyBytes || disassemblySize == 0)
				return;

			const std::string disassembly(disassemblyBytes, disassemblySize);
			std::istringstream lines(disassembly);
			std::vector<std::string> normalizedInstructions;
			std::string line;
			bool insideFunction = false;

			const std::regex assignmentPattern(R"(^\s*%[0-9]+\s*=\s*)");
			const std::regex ssaIdentifierPattern(R"(%[0-9]+)");
			const std::regex metadataAttachmentPattern(R"(\s*,\s*![A-Za-z0-9_.-]+\s+![0-9]+)");
			const std::regex metadataIdentifierPattern(R"(![0-9]+)");
			const std::regex numericLabelPattern(R"(^[0-9]+:$)");

			while (std::getline(lines, line))
			{
				const std::string trimmedLine = StringHelper::CollapseWhitespace(line);

				if (!insideFunction)
				{
					if (trimmedLine.rfind("define ", 0) != 0)
						continue;

					insideFunction = true;

					if (analysis.entryFunctionName.empty())
					{
						const size_t nameStart = trimmedLine.find('@');
						const size_t nameEnd = nameStart == std::string::npos ? std::string::npos : trimmedLine.find('(', nameStart + 1);
						if (nameStart != std::string::npos && nameEnd != std::string::npos)
							analysis.entryFunctionName = trimmedLine.substr(nameStart + 1, nameEnd - nameStart - 1);
					}

					continue;
				}

				if (trimmedLine == "}")
				{
					insideFunction = false;
					continue;
				}

				std::string instruction = line.substr(0, line.find(';'));
				instruction = std::regex_replace(instruction, metadataAttachmentPattern, "");
				instruction = std::regex_replace(instruction, assignmentPattern, "");
				instruction = std::regex_replace(instruction, ssaIdentifierPattern, "%v");
				instruction = std::regex_replace(instruction, metadataIdentifierPattern, "!m");
				instruction = StringHelper::CollapseWhitespace(instruction);

				if (instruction.empty())
					continue;

				if (std::regex_match(instruction, numericLabelPattern))
					instruction = "label:";

				normalizedInstructions.push_back(std::move(instruction));
			}

			analysis.normalizedInstructionCount = static_cast<uint32_t>((std::min)(
				normalizedInstructions.size(),
				static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));

			std::sort(normalizedInstructions.begin(), normalizedInstructions.end());
			normalizedInstructions.erase(std::unique(normalizedInstructions.begin(), normalizedInstructions.end()), normalizedInstructions.end());
			analysis.uniqueNormalizedInstructionCount = static_cast<uint32_t>((std::min)(
				normalizedInstructions.size(),
				static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));

			std::ostringstream canonicalInstructions;
			for (const std::string& instruction : normalizedInstructions)
				canonicalInstructions << instruction << '\n';

			if (!normalizedInstructions.empty())
				analysis.semanticInstructionSetHash = Fingerprint(canonicalInstructions.str());
		}
	}

	bool Analyze(
		const void* bytecode,
		size_t bytecodeLength,
		ShaderAnalysis::ShaderAnalysisDisk& outAnalysis,
		const std::unordered_set<std::string>* acceptablePortableReflectionHashes)
	{
		// DXC may also be used by template creation and cached-PSO discovery on game
		// threads. Keep all reflection/disassembly work single-file across the process.
		std::lock_guard<std::mutex> analyzerLock(gAnalyzerMutex);

		outAnalysis = {};

		if (!bytecode || bytecodeLength == 0)
			return Fail(outAnalysis, "Shader bytecode is empty.");

		if (bytecodeLength > (std::numeric_limits<uint32_t>::max)())
			return Fail(outAnalysis, "Shader bytecode exceeds the DXC blob size limit.");

		const HMODULE dxCompilerModule = LoadDXCompiler();

		if (!dxCompilerModule)
			return Fail(outAnalysis, "dxcompiler.dll could not be loaded from ShaderInjector/Tools or the process DLL search path.");

		const auto createInstance = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(dxCompilerModule, "DxcCreateInstance"));

		if (!createInstance)
			return Fail(outAnalysis, "dxcompiler.dll does not export DxcCreateInstance.");

		ComPtr<IDxcUtils> utilities;
		HRESULT result = createInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utilities));

		if (FAILED(result))
			return Fail(outAnalysis, "Failed to create IDxcUtils: " + StringHelper::FormatHRESULT(result));

		ComPtr<IDxcBlobEncoding> shaderBlob;
		result = utilities->CreateBlobFromPinned(bytecode, static_cast<uint32_t>(bytecodeLength), DXC_CP_ACP, &shaderBlob);

		if (FAILED(result))
			return Fail(outAnalysis, "Failed to create the DXC shader blob: " + StringHelper::FormatHRESULT(result));

		ComPtr<IDxcContainerReflection> containerReflection;
		result = createInstance(CLSID_DxcContainerReflection, IID_PPV_ARGS(&containerReflection));

		if (FAILED(result))
			return Fail(outAnalysis, "Failed to create IDxcContainerReflection: " + StringHelper::FormatHRESULT(result));

		result = containerReflection->Load(shaderBlob.Get());

		if (FAILED(result))
			return Fail(outAnalysis, "The captured shader is not a readable DXIL container: " + StringHelper::FormatHRESULT(result));

		uint32_t partCount = 0;
		result = containerReflection->GetPartCount(&partCount);

		if (FAILED(result))
			return Fail(outAnalysis, "Failed to enumerate DXIL container parts: " + StringHelper::FormatHRESULT(result));

		std::ostringstream containerSignature;
		outAnalysis.containerParts.reserve(partCount);

		for (uint32_t partIndex = 0; partIndex < partCount; ++partIndex)
		{
			ShaderAnalysis::ContainerPartDisk part{};

			if (FAILED(containerReflection->GetPartKind(partIndex, &part.kind)))
				continue;

			part.fourCC = FourCCText(part.kind);
			ComPtr<IDxcBlob> partContent;

			if (SUCCEEDED(containerReflection->GetPartContent(partIndex, &partContent)) && partContent)
			{
				part.size = partContent->GetBufferSize();

				if (part.size > 0)
					part.contentHash = Hash::FormatHash(Hash::HashMemory(partContent->GetBufferPointer(), static_cast<size_t>(part.size)));
			}

			containerSignature << part.fourCC << ':' << part.size << ';';
			outAnalysis.containerParts.push_back(std::move(part));
		}

		outAnalysis.containerSignatureHash = Fingerprint(containerSignature.str());

		uint32_t dxilPartIndex = 0;
		result = containerReflection->FindFirstPartKind(DXC_PART_DXIL, &dxilPartIndex);

		if (FAILED(result))
			return Fail(outAnalysis, "The shader container does not contain a DXIL program part: " + StringHelper::FormatHRESULT(result));

		ComPtr<ID3D12ShaderReflection> shaderReflection;
		result = containerReflection->GetPartReflection(dxilPartIndex, IID_PPV_ARGS(&shaderReflection));

		if (FAILED(result))
			return Fail(outAnalysis, "DXIL shader reflection failed: " + StringHelper::FormatHRESULT(result));

		D3D12_SHADER_DESC shaderDescription{};
		result = shaderReflection->GetDesc(&shaderDescription);

		if (FAILED(result))
			return Fail(outAnalysis, "Failed to read the DXIL shader description: " + StringHelper::FormatHRESULT(result));

		outAnalysis.creator = StringHelper::SafeString(shaderDescription.Creator);
		outAnalysis.shaderVersionToken = shaderDescription.Version;
		outAnalysis.shaderStage = D3D12_SHVER_GET_TYPE(shaderDescription.Version);
		outAnalysis.shaderModelMajor = D3D12_SHVER_GET_MAJOR(shaderDescription.Version);
		outAnalysis.shaderModelMinor = D3D12_SHVER_GET_MINOR(shaderDescription.Version);
		const auto shaderStageText = ShaderStageText(outAnalysis.shaderStage);
		outAnalysis.shaderStageName = shaderStageText.first;
		outAnalysis.shaderModelProfile = std::string(shaderStageText.second) + "_" + std::to_string(outAnalysis.shaderModelMajor) + "_" + std::to_string(outAnalysis.shaderModelMinor);
		outAnalysis.compilationFlags = shaderDescription.Flags;

		auto readSignature = [&](uint32_t count, auto getter, std::vector<ShaderAnalysis::SignatureParameterDisk>& destination)
		{
			destination.reserve(count);

			for (uint32_t index = 0; index < count; ++index)
			{
				D3D12_SIGNATURE_PARAMETER_DESC description{};

				if (SUCCEEDED((shaderReflection.Get()->*getter)(index, &description)))
					destination.push_back(ReadSignatureParameter(description));
			}
		};

		readSignature(shaderDescription.InputParameters, &ID3D12ShaderReflection::GetInputParameterDesc, outAnalysis.inputParameters);
		readSignature(shaderDescription.OutputParameters, &ID3D12ShaderReflection::GetOutputParameterDesc, outAnalysis.outputParameters);
		readSignature(shaderDescription.PatchConstantParameters, &ID3D12ShaderReflection::GetPatchConstantParameterDesc, outAnalysis.patchConstantParameters);

		outAnalysis.resourceBindings.reserve(shaderDescription.BoundResources);

		for (uint32_t resourceIndex = 0; resourceIndex < shaderDescription.BoundResources; ++resourceIndex)
		{
			D3D12_SHADER_INPUT_BIND_DESC description{};

			if (FAILED(shaderReflection->GetResourceBindingDesc(resourceIndex, &description)))
				continue;

			ShaderAnalysis::ResourceBindingDisk resource{};
			resource.name = StringHelper::SafeString(description.Name);
			resource.type = static_cast<uint32_t>(description.Type);
			resource.bindPoint = description.BindPoint;
			resource.bindCount = description.BindCount;
			resource.flags = description.uFlags;
			resource.returnType = static_cast<uint32_t>(description.ReturnType);
			resource.dimension = static_cast<uint32_t>(description.Dimension);
			resource.sampleCountOrStride = description.NumSamples;
			resource.registerSpace = description.Space;
			resource.rangeId = description.uID;
			outAnalysis.resourceBindings.push_back(std::move(resource));
		}

		outAnalysis.constantBuffers.reserve(shaderDescription.ConstantBuffers);

		for (uint32_t bufferIndex = 0; bufferIndex < shaderDescription.ConstantBuffers; ++bufferIndex)
		{
			ID3D12ShaderReflectionConstantBuffer* reflectedBuffer = shaderReflection->GetConstantBufferByIndex(bufferIndex);

			if (!reflectedBuffer)
				continue;

			D3D12_SHADER_BUFFER_DESC bufferDescription{};

			if (FAILED(reflectedBuffer->GetDesc(&bufferDescription)))
				continue;

			ShaderAnalysis::ConstantBufferDisk buffer{};
			buffer.name = StringHelper::SafeString(bufferDescription.Name);
			buffer.type = static_cast<uint32_t>(bufferDescription.Type);
			buffer.size = bufferDescription.Size;
			buffer.flags = bufferDescription.uFlags;
			buffer.variables.reserve(bufferDescription.Variables);

			for (uint32_t variableIndex = 0; variableIndex < bufferDescription.Variables; ++variableIndex)
			{
				ID3D12ShaderReflectionVariable* reflectedVariable = reflectedBuffer->GetVariableByIndex(variableIndex);

				if (!reflectedVariable)
					continue;

				D3D12_SHADER_VARIABLE_DESC variableDescription{};
				if (FAILED(reflectedVariable->GetDesc(&variableDescription)))
					continue;

				ShaderAnalysis::ConstantBufferVariableDisk variable{};
				variable.name = StringHelper::SafeString(variableDescription.Name);
				variable.startOffset = variableDescription.StartOffset;
				variable.size = variableDescription.Size;
				variable.flags = variableDescription.uFlags;
				variable.startTexture = variableDescription.StartTexture;
				variable.textureCount = variableDescription.TextureSize;
				variable.startSampler = variableDescription.StartSampler;
				variable.samplerCount = variableDescription.SamplerSize;
				variable.typeLayout = ReadTypeLayout(reflectedVariable->GetType(), 0);
				buffer.variables.push_back(std::move(variable));
			}

			outAnalysis.constantBuffers.push_back(std::move(buffer));
		}

		auto& statistics = outAnalysis.instructionStatistics;
		statistics.instructionCount = shaderDescription.InstructionCount;
		statistics.temporaryRegisterCount = shaderDescription.TempRegisterCount;
		statistics.temporaryArrayCount = shaderDescription.TempArrayCount;
		statistics.constantDefinitionCount = shaderDescription.DefCount;
		statistics.declarationCount = shaderDescription.DclCount;
		statistics.textureNormalInstructionCount = shaderDescription.TextureNormalInstructions;
		statistics.textureLoadInstructionCount = shaderDescription.TextureLoadInstructions;
		statistics.textureComparisonInstructionCount = shaderDescription.TextureCompInstructions;
		statistics.textureBiasInstructionCount = shaderDescription.TextureBiasInstructions;
		statistics.textureGradientInstructionCount = shaderDescription.TextureGradientInstructions;
		statistics.floatInstructionCount = shaderDescription.FloatInstructionCount;
		statistics.signedIntegerInstructionCount = shaderDescription.IntInstructionCount;
		statistics.unsignedIntegerInstructionCount = shaderDescription.UintInstructionCount;
		statistics.staticFlowControlCount = shaderDescription.StaticFlowControlCount;
		statistics.dynamicFlowControlCount = shaderDescription.DynamicFlowControlCount;
		statistics.macroInstructionCount = shaderDescription.MacroInstructionCount;
		statistics.arrayInstructionCount = shaderDescription.ArrayInstructionCount;
		statistics.cutInstructionCount = shaderDescription.CutInstructionCount;
		statistics.emitInstructionCount = shaderDescription.EmitInstructionCount;
		statistics.barrierInstructionCount = shaderDescription.cBarrierInstructions;
		statistics.interlockedInstructionCount = shaderDescription.cInterlockedInstructions;
		statistics.textureStoreInstructionCount = shaderDescription.cTextureStoreInstructions;

		auto& execution = outAnalysis.executionProperties;
		execution.geometryOutputTopology = static_cast<uint32_t>(shaderDescription.GSOutputTopology);
		execution.geometryMaximumOutputVertexCount = shaderDescription.GSMaxOutputVertexCount;
		execution.inputPrimitive = static_cast<uint32_t>(shaderDescription.InputPrimitive);
		execution.geometryInstanceCount = shaderDescription.cGSInstanceCount;
		execution.controlPointCount = shaderDescription.cControlPoints;
		execution.hullOutputPrimitive = static_cast<uint32_t>(shaderDescription.HSOutputPrimitive);
		execution.hullPartitioning = static_cast<uint32_t>(shaderDescription.HSPartitioning);
		execution.tessellatorDomain = static_cast<uint32_t>(shaderDescription.TessellatorDomain);
		execution.threadGroupTotalSize = shaderReflection->GetThreadGroupSize(&execution.threadGroupSizeX, &execution.threadGroupSizeY, &execution.threadGroupSizeZ);
		execution.requiresFlags = shaderReflection->GetRequiresFlags();
		execution.sampleFrequencyShader = shaderReflection->IsSampleFrequencyShader() != FALSE;

		D3D_FEATURE_LEVEL minimumFeatureLevel{};

		if (SUCCEEDED(shaderReflection->GetMinFeatureLevel(&minimumFeatureLevel)))
			execution.minimumFeatureLevel = static_cast<uint32_t>(minimumFeatureLevel);

		outAnalysis.interfaceSignatureHash = BuildSignatureParameterFingerprint(outAnalysis.inputParameters, outAnalysis.outputParameters, outAnalysis.patchConstantParameters);
		outAnalysis.resourceSignatureHash = BuildResourceFingerprint(outAnalysis.resourceBindings);
		outAnalysis.constantBufferSignatureHash = BuildConstantBufferFingerprint(outAnalysis.constantBuffers);
		outAnalysis.instructionStatisticsHash = BuildInstructionFingerprint(outAnalysis.instructionStatistics);
		outAnalysis.executionSignatureHash = BuildExecutionFingerprint(outAnalysis.executionProperties);

		// portableReflectionIdentityHash only needs the structured reflection data gathered
		// above - no disassembly required - so it's available here as a cheap pre-filter before
		// the expensive full-disassembly step below.
		std::ostringstream portableReflectionIdentity;
		portableReflectionIdentity << outAnalysis.shaderStage << ':' << outAnalysis.shaderModelMajor << ':' << outAnalysis.shaderModelMinor << ':'
			<< outAnalysis.interfaceSignatureHash << ':' << outAnalysis.resourceSignatureHash << ':'
			<< outAnalysis.constantBufferSignatureHash << ':' << outAnalysis.executionSignatureHash;
		outAnalysis.portableReflectionIdentityHash = Fingerprint(portableReflectionIdentity.str());

		// Cross-version matches always share an exact portableReflectionIdentityHash (this is
		// already how the fuzzy replacement fallback gates its candidates). If nothing in the
		// caller's candidate set shares this shader's reflection identity, it can never match, so
		// skip the expensive disassembly and semantic hashing below entirely.
		const bool skipDisassembly = acceptablePortableReflectionHashes && acceptablePortableReflectionHashes->find(outAnalysis.portableReflectionIdentityHash) == acceptablePortableReflectionHashes->end();

		if (!skipDisassembly)
		{
			AnalyzeSemanticInstructions(createInstance, shaderBlob.Get(), outAnalysis);

			if (!outAnalysis.semanticInstructionSetHash.empty())
			{
				outAnalysis.crossVersionIdentityHash = Fingerprint(outAnalysis.portableReflectionIdentityHash + ":" + outAnalysis.entryFunctionName + ":" + outAnalysis.semanticInstructionSetHash);
			}
		}

		std::ostringstream reflectionSignature;
		reflectionSignature << outAnalysis.shaderStage << ':' << outAnalysis.shaderModelMajor << ':' << outAnalysis.shaderModelMinor << ':'
			<< outAnalysis.interfaceSignatureHash << ':' << outAnalysis.resourceSignatureHash << ':'
			<< outAnalysis.constantBufferSignatureHash << ':' << outAnalysis.instructionStatisticsHash << ':'
			<< execution.requiresFlags << ':' << execution.threadGroupSizeX << ':' << execution.threadGroupSizeY << ':'
			<< execution.threadGroupSizeZ << ':' << execution.sampleFrequencyShader;
		outAnalysis.reflectionSignatureHash = Fingerprint(reflectionSignature.str());

		outAnalysis.succeeded = true;
		outAnalysis.error.clear();
		return true;
	}
}
