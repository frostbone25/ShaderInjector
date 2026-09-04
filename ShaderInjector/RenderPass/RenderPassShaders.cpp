#include "RenderPass/RenderPassShaders.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

#include <d3d12shader.h>

#include "IO/ShaderInjectorIO.h"
#include "ShaderResource/DatabaseShaderResources.h"
#include "ShaderResource/ShaderResourceCatalog.h"
#include "StringHelper.h"

namespace RenderPassShaders
{
	namespace
	{
		constexpr const char* vertexSourceFile = "FullscreenTriangleVS.hlsl";
		constexpr const char* fragmentSourceFile = "Fragment.hlsl";
		constexpr const char* vertexBlobFile = "FullscreenTriangleVS.blob";
		constexpr const char* fragmentBlobFile = "Fragment.blob";
		constexpr const char* downsampleFragmentSourceFile = "Downsample.hlsl";
		constexpr const char* downsampleFragmentBlobFile = "Downsample.blob";
		constexpr const char* upsampleFragmentSourceFile = "UpsampleChain.hlsl";
		constexpr const char* upsampleFragmentBlobFile = "UpsampleChain.blob";
		constexpr const char* downsampleComputeSourceFile = "DownsampleCS.hlsl";
		constexpr const char* downsampleComputeBlobFile = "DownsampleCS.blob";
		constexpr const char* upsampleComputeSourceFile = "UpsampleChainCS.hlsl";
		constexpr const char* upsampleComputeBlobFile = "UpsampleChainCS.blob";
		constexpr const char* mipChainVertexSourceFile = "MipChainVS.hlsl";
		constexpr const char* mipChainFragmentSourceFile = "MipChainDownsample.hlsl";
		constexpr const char* mipChainVertexBlobFile = "MipChainVS.blob";
		constexpr const char* mipChainFragmentBlobFile = "MipChainDownsample.blob";
		constexpr const char* mipChainComputeSourceFile = "MipChainDownsampleCS.hlsl";
		constexpr const char* mipChainComputeBlobFile = "MipChainDownsampleCS.blob";
		constexpr const char* replacementPixelSourceFile = "ReplacementPixelShader.hlsl";
		constexpr const char* replacementPixelBlobFile = "ReplacementPixelShader.blob";
		constexpr const char* replacementComputeSourceFile = "ReplacementComputeShader.hlsl";
		constexpr const char* replacementComputeBlobFile = "ReplacementComputeShader.blob";
		constexpr const char* computeSourceFile = "Compute.hlsl";
		constexpr const char* computeBlobFile = "Compute.blob";

		const char* fullscreenTriangleVertexShader = R"(// Procedural fullscreen triangle. No vertex buffer is required.
struct FullscreenVertexOutput
{
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
};

FullscreenVertexOutput main(uint vertexId : SV_VertexID)
{
	FullscreenVertexOutput output;
	float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
	output.position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
	output.uv = uv;
	return output;
}
)";

		const char* mipChainFragmentShader = R"(// Generates one destination level of the injector-owned mip texture.
// Mip zero copies the game texture. Later levels use the editable 2x2 filter below.
Texture2D<float4> SI_SourceTexture : register(t0);
SamplerState SI_LinearClampSampler : register(s0);

cbuffer SI_MipChainConstants : register(b0)
{
	uint SI_CopyBaseLevel;
	uint SI_SourceWidth;
	uint SI_SourceHeight;
	uint SI_Reserved;
};

struct FullscreenVertexOutput
{
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
};

float4 main(FullscreenVertexOutput input) : SV_Target0
{
	if (SI_CopyBaseLevel != 0)
		return SI_SourceTexture.SampleLevel(SI_LinearClampSampler, input.uv, 0.0);

	float2 texelSize = rcp(float2(max(SI_SourceWidth, 1u), max(SI_SourceHeight, 1u)));
	float2 halfTexel = texelSize * 0.5;
	return (
		SI_SourceTexture.SampleLevel(SI_LinearClampSampler, input.uv + float2(-halfTexel.x, -halfTexel.y), 0.0) +
		SI_SourceTexture.SampleLevel(SI_LinearClampSampler, input.uv + float2( halfTexel.x, -halfTexel.y), 0.0) +
		SI_SourceTexture.SampleLevel(SI_LinearClampSampler, input.uv + float2(-halfTexel.x,  halfTexel.y), 0.0) +
		SI_SourceTexture.SampleLevel(SI_LinearClampSampler, input.uv + float2( halfTexel.x,  halfTexel.y), 0.0)) * 0.25;
}
)";

		const char* mipChainComputeShader = R"(// Generates one destination level of the injector-owned mip texture.
// Mip zero copies the game texture. Later levels use the editable 2x2 filter below.
Texture2D<float4> SI_SourceTexture : register(t0);
RWTexture2D<float4> SI_DestinationTexture : register(u0);

cbuffer SI_MipChainConstants : register(b0)
{
	uint SI_CopyBaseLevel;
	uint SI_SourceWidth;
	uint SI_SourceHeight;
	uint SI_Reserved;
};

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint destinationWidth, destinationHeight;
	SI_DestinationTexture.GetDimensions(destinationWidth, destinationHeight);
	if (dispatchThreadId.x >= destinationWidth || dispatchThreadId.y >= destinationHeight)
		return;

	uint2 destinationPixel = dispatchThreadId.xy;
	if (SI_CopyBaseLevel != 0)
	{
		SI_DestinationTexture[destinationPixel] = SI_SourceTexture.Load(int3(destinationPixel, 0));
		return;
	}

	int2 maximumPixel = int2(max(SI_SourceWidth, 1u) - 1u, max(SI_SourceHeight, 1u) - 1u);
	int2 sourceBase = int2(destinationPixel * 2u);
	int2 p00 = clamp(sourceBase, int2(0, 0), maximumPixel);
	int2 p10 = clamp(sourceBase + int2(1, 0), int2(0, 0), maximumPixel);
	int2 p01 = clamp(sourceBase + int2(0, 1), int2(0, 0), maximumPixel);
	int2 p11 = clamp(sourceBase + int2(1, 1), int2(0, 0), maximumPixel);
	SI_DestinationTexture[destinationPixel] = (
		SI_SourceTexture.Load(int3(p00, 0)) +
		SI_SourceTexture.Load(int3(p10, 0)) +
		SI_SourceTexture.Load(int3(p01, 0)) +
		SI_SourceTexture.Load(int3(p11, 0))) * 0.25;
}
)";

		std::string SanitizeIdentifier(const std::string& value, const std::string& fallback)
		{
			std::string identifier = "SI_";
			for (char character : value)
			{
				const unsigned char unsignedCharacter = static_cast<unsigned char>(character);
				identifier.push_back(std::isalnum(unsignedCharacter) || character == '_' ? character : '_');
			}

			if (identifier == "SI_")
				identifier += fallback;
			return identifier;
		}

		std::string UniqueIdentifier(
			const ShaderAnalysis::ResourceBindingDisk& resource,
			std::unordered_set<std::string>& usedIdentifiers)
		{
			const std::string fallback = "Resource_" + std::to_string(resource.type) + "_" +
				std::to_string(resource.bindPoint) + "_" + std::to_string(resource.registerSpace);
			const std::string baseIdentifier = SanitizeIdentifier(resource.name, fallback);
			std::string identifier = baseIdentifier;
			for (uint32_t suffix = 2; !usedIdentifiers.insert(identifier).second; ++suffix)
				identifier = baseIdentifier + "_" + std::to_string(suffix);
			return identifier;
		}

		const ShaderAnalysis::ShaderAnalysisDisk* SelectReflectionAnalysis(
			const ModifiedShader::PackageDisk& modifiedShader)
		{
			const ShaderAnalysis::ShaderAnalysisDisk* selected = nullptr;
			size_t selectedInformationCount = 0;
			for (const ModifiedShader::TargetDisk& target : modifiedShader.targets)
			{
				const ShaderAnalysis::ShaderAnalysisDisk& analysis = target.shaderAnalysis;
				if (!analysis.succeeded)
					continue;

				const size_t informationCount = analysis.resourceBindings.size() + analysis.constantBuffers.size();
				if (!selected || informationCount > selectedInformationCount)
				{
					selected = &analysis;
					selectedInformationCount = informationCount;
				}
			}
			return selected;
		}

		const ShaderAnalysis::ConstantBufferDisk* FindConstantBuffer(
			const ShaderAnalysis::ShaderAnalysisDisk& analysis,
			const std::string& name)
		{
			for (const ShaderAnalysis::ConstantBufferDisk& constantBuffer : analysis.constantBuffers)
			{
				if (constantBuffer.name == name)
					return &constantBuffer;
			}
			return nullptr;
		}

		const char* TypedResourceElementType(uint32_t returnType)
		{
			switch (static_cast<D3D_RESOURCE_RETURN_TYPE>(returnType))
			{
				case D3D_RETURN_TYPE_SINT: return "int4";
				case D3D_RETURN_TYPE_UINT: return "uint4";
				case D3D_RETURN_TYPE_DOUBLE: return "double4";
				default: return "float4";
			}
		}

		std::string TextureType(
			uint32_t dimension,
			const char* elementType,
			bool writable,
			uint32_t sampleCount)
		{
			const char* prefix = writable ? "RWTexture" : "Texture";
			switch (static_cast<D3D_SRV_DIMENSION>(dimension))
			{
				case D3D_SRV_DIMENSION_TEXTURE1D: return std::string(prefix) + "1D<" + elementType + ">";
				case D3D_SRV_DIMENSION_TEXTURE1DARRAY: return std::string(prefix) + "1DArray<" + elementType + ">";
				case D3D_SRV_DIMENSION_TEXTURE2D: return std::string(prefix) + "2D<" + elementType + ">";
				case D3D_SRV_DIMENSION_TEXTURE2DARRAY: return std::string(prefix) + "2DArray<" + elementType + ">";
				case D3D_SRV_DIMENSION_TEXTURE3D: return std::string(prefix) + "3D<" + elementType + ">";
				case D3D_SRV_DIMENSION_TEXTURECUBE: return writable ? "RWTexture2D<float4>" : std::string("TextureCube<") + elementType + ">";
				case D3D_SRV_DIMENSION_TEXTURECUBEARRAY: return writable ? "RWTexture2DArray<float4>" : std::string("TextureCubeArray<") + elementType + ">";
				case D3D_SRV_DIMENSION_TEXTURE2DMS:
					return std::string("Texture2DMS<") + elementType + ", " + std::to_string((std::max)(1u, sampleCount)) + ">";
				case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY:
					return std::string("Texture2DMSArray<") + elementType + ", " + std::to_string((std::max)(1u, sampleCount)) + ">";
				default: return writable ? std::string("RWBuffer<") + elementType + ">" : std::string("Buffer<") + elementType + ">";
			}
		}

		std::string ArraySuffix(uint32_t bindCount)
		{
			if (bindCount == 1)
				return {};
			if (bindCount == 0 || bindCount == UINT32_MAX)
				return "[]";
			return "[" + std::to_string(bindCount) + "]";
		}

		std::string RegisterText(char registerType, const ShaderAnalysis::ResourceBindingDisk& resource)
		{
			return "register(" + std::string(1, registerType) + std::to_string(resource.bindPoint) +
				", space" + std::to_string(resource.registerSpace) + ")";
		}

		void AppendResourceDeclaration(
			std::ostringstream& source,
			const ShaderAnalysis::ShaderAnalysisDisk& analysis,
			const ShaderAnalysis::ResourceBindingDisk& resource,
			std::unordered_set<std::string>& usedIdentifiers)
		{
			const std::string identifier = UniqueIdentifier(resource, usedIdentifiers);
			const D3D_SHADER_INPUT_TYPE inputType = static_cast<D3D_SHADER_INPUT_TYPE>(resource.type);
			source << "// Reflected resource: " << (resource.name.empty() ? "(unnamed)" : resource.name)
				<< ", bind count " << resource.bindCount << "\n";

			if (inputType == D3D_SIT_CBUFFER)
			{
				const ShaderAnalysis::ConstantBufferDisk* constantBuffer = FindConstantBuffer(analysis, resource.name);
				const uint32_t byteSize = constantBuffer ? constantBuffer->size : 16;
				const uint32_t vectorCount = (std::max)(1u, (byteSize + 15u) / 16u);
				source << "cbuffer " << identifier << " : " << RegisterText('b', resource) << "\n{\n"
					<< "\tuint4 " << identifier << "_RawData[" << vectorCount << "];\n};\n\n";
				return;
			}

			std::string declarationType;
			char registerType = 't';
			const char* elementType = TypedResourceElementType(resource.returnType);
			switch (inputType)
			{
				case D3D_SIT_TBUFFER:
				case D3D_SIT_TEXTURE:
					declarationType = TextureType(resource.dimension, elementType, false, resource.sampleCountOrStride);
					break;
				case D3D_SIT_SAMPLER:
					declarationType = (resource.flags & D3D_SIF_COMPARISON_SAMPLER) != 0 ? "SamplerComparisonState" : "SamplerState";
					registerType = 's';
					break;
				case D3D_SIT_UAV_RWTYPED:
					declarationType = TextureType(resource.dimension, elementType, true, resource.sampleCountOrStride);
					registerType = 'u';
					break;
				case D3D_SIT_STRUCTURED: declarationType = "StructuredBuffer<uint>"; break;
				case D3D_SIT_UAV_RWSTRUCTURED: declarationType = "RWStructuredBuffer<uint>"; registerType = 'u'; break;
				case D3D_SIT_BYTEADDRESS: declarationType = "ByteAddressBuffer"; break;
				case D3D_SIT_UAV_RWBYTEADDRESS: declarationType = "RWByteAddressBuffer"; registerType = 'u'; break;
				case D3D_SIT_UAV_APPEND_STRUCTURED: declarationType = "AppendStructuredBuffer<uint>"; registerType = 'u'; break;
				case D3D_SIT_UAV_CONSUME_STRUCTURED: declarationType = "ConsumeStructuredBuffer<uint>"; registerType = 'u'; break;
				case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER: declarationType = "RWStructuredBuffer<uint>"; registerType = 'u'; break;
				case D3D_SIT_RTACCELERATIONSTRUCTURE: declarationType = "RaytracingAccelerationStructure"; break;
				default:
					source << "// Unsupported reflected resource type " << resource.type << "; declare it manually if needed.\n\n";
					return;
			}

			source << declarationType << ' ' << identifier << ArraySuffix(resource.bindCount)
				<< " : " << RegisterText(registerType, resource) << ";\n\n";
		}

		void AppendInjectedResourceDeclarations(
			std::ostringstream& source,
			const RenderPass::RenderPassDisk& renderPass,
			std::unordered_set<std::string>& usedIdentifiers)
		{
			const bool hasRuntimeInputs = std::any_of(
				renderPass.inputs.begin(),
				renderPass.inputs.end(),
				[](const RenderPass::LogicalResourceBindingDisk& input)
				{
					return input.origin == ShaderResource::ResourceOrigin::Runtime &&
						input.access == RenderPass::ResourceAccess::ShaderResource;
				});
			const bool hasRuntimeOutputs = std::any_of(
				renderPass.outputs.begin(),
				renderPass.outputs.end(),
				[](const RenderPass::LogicalResourceBindingDisk& output)
				{
					return output.origin == ShaderResource::ResourceOrigin::Runtime &&
						output.access == RenderPass::ResourceAccess::UnorderedAccess;
				});
			if (renderPass.shaderResources.empty() && renderPass.samplers.empty() &&
				!hasRuntimeInputs && !hasRuntimeOutputs)
				return;
			if (!renderPass.samplers.empty())
				source << "// Injector-owned sampler states configured on this Render Pass.\n";
			for (size_t samplerIndex = 0; samplerIndex < renderPass.samplers.size(); ++samplerIndex)
			{
				const RenderPass::SamplerStateDisk& sampler = renderPass.samplers[samplerIndex];
				const std::string fallback = "Sampler_" + std::to_string(sampler.shaderRegister);
				std::string identifier = SanitizeIdentifier(sampler.hlslName, fallback);
				const std::string baseIdentifier = identifier;
				for (uint32_t suffix = 2; !usedIdentifiers.insert(identifier).second; ++suffix)
					identifier = baseIdentifier + "_" + std::to_string(suffix);
				source << (sampler.comparisonSampler ? "SamplerComparisonState " : "SamplerState ")
					<< identifier << " : register(s" << sampler.shaderRegister
					<< ", space" << sampler.registerSpace << ");\n";
			}
			if (!renderPass.shaderResources.empty())
				source << "// Injector-owned DDS textures configured on this Render Pass.\n";
			for (const RenderPass::ShaderResourceReferenceDisk& resource : renderPass.shaderResources)
			{
				const std::string fallback = "Texture_" + std::to_string(resource.shaderRegister);
				std::string identifier = SanitizeIdentifier(resource.hlslName, fallback);
				const std::string baseIdentifier = identifier;
				for (uint32_t suffix = 2; !usedIdentifiers.insert(identifier).second; ++suffix)
					identifier = baseIdentifier + "_" + std::to_string(suffix);
				const ShaderResource::TextureDisk* texture =
					DatabaseShaderResources::FindShaderResourceById(resource.resourceId);
				const ShaderResource::TextureDimension dimension = texture
					? texture->dimension
					: ShaderResource::TextureDimension::Unknown;
				source << ShaderResource::TextureHlslTypeName(dimension) << ' ' << identifier << " : register(t"
					<< resource.shaderRegister << ", space" << resource.registerSpace << ");\n";
			}

			if (hasRuntimeInputs)
				source << "// Runtime textures produced by earlier Render Passes.\n";
			const std::vector<ShaderResource::CatalogEntry> catalog = ShaderResourceCatalog::GetSnapshot();
			for (size_t inputIndex = 0; inputIndex < renderPass.inputs.size(); ++inputIndex)
			{
				const RenderPass::LogicalResourceBindingDisk& input = renderPass.inputs[inputIndex];
				if (input.origin != ShaderResource::ResourceOrigin::Runtime ||
					input.access != RenderPass::ResourceAccess::ShaderResource)
				{
					continue;
				}
				const std::string fallback = "RuntimeInput_" + std::to_string(inputIndex);
				std::string identifier = SanitizeIdentifier(input.hlslName, fallback);
				const std::string baseIdentifier = identifier;
				for (uint32_t suffix = 2; !usedIdentifiers.insert(identifier).second; ++suffix)
					identifier = baseIdentifier + "_" + std::to_string(suffix);
				ShaderResource::TextureDimension dimension = ShaderResource::TextureDimension::Texture2D;
				const auto catalogIt = std::find_if(catalog.begin(), catalog.end(), [&](const auto& entry)
				{
					return entry.origin == ShaderResource::ResourceOrigin::Runtime && entry.id == input.resourceId;
				});
				if (catalogIt != catalog.end() && catalogIt->dimension != ShaderResource::TextureDimension::Unknown)
					dimension = catalogIt->dimension;
				source << ShaderResource::TextureHlslTypeName(dimension) << ' ' << identifier << " : register(t"
					<< input.shaderRegister << ", space" << input.registerSpace << ");\n";
			}

			if (hasRuntimeOutputs)
				source << "// Writable runtime textures produced by this Render Pass.\n";
			for (size_t outputIndex = 0; outputIndex < renderPass.outputs.size(); ++outputIndex)
			{
				const RenderPass::LogicalResourceBindingDisk& output = renderPass.outputs[outputIndex];
				if (output.origin != ShaderResource::ResourceOrigin::Runtime ||
					output.access != RenderPass::ResourceAccess::UnorderedAccess)
				{
					continue;
				}

				const std::string fallback = "RuntimeOutput_" + std::to_string(outputIndex);
				std::string identifier = SanitizeIdentifier(output.hlslName, fallback);
				const std::string baseIdentifier = identifier;
				for (uint32_t suffix = 2; !usedIdentifiers.insert(identifier).second; ++suffix)
					identifier = baseIdentifier + "_" + std::to_string(suffix);

				ShaderResource::TextureDimension dimension = ShaderResource::TextureDimension::Texture2D;
				const auto configuredTexture = std::find_if(
					renderPass.runtimeResources.begin(),
					renderPass.runtimeResources.end(),
					[&](const auto& resource) { return resource.id == output.resourceId; });
				if (configuredTexture != renderPass.runtimeResources.end())
					dimension = configuredTexture->texture.dimension;
				else
				{
					const auto catalogIt = std::find_if(catalog.begin(), catalog.end(), [&](const auto& entry)
					{
						return entry.origin == ShaderResource::ResourceOrigin::Runtime && entry.id == output.resourceId;
					});
					if (catalogIt != catalog.end() && catalogIt->dimension != ShaderResource::TextureDimension::Unknown)
						dimension = catalogIt->dimension;
				}

				const char* textureType = "RWTexture2D<float4>";
				if (dimension == ShaderResource::TextureDimension::Texture2DArray ||
					dimension == ShaderResource::TextureDimension::TextureCube ||
					dimension == ShaderResource::TextureDimension::TextureCubeArray)
				{
					textureType = "RWTexture2DArray<float4>";
				}
				else if (dimension == ShaderResource::TextureDimension::Texture3D)
					textureType = "RWTexture3D<float4>";

				source << textureType << ' ' << identifier << " : register(u"
					<< output.shaderRegister << ", space" << output.registerSpace << ");\n";
			}
			source << '\n';
		}

		bool IsInjectedTextureBinding(
			const RenderPass::RenderPassDisk& renderPass,
			const ShaderAnalysis::ResourceBindingDisk& resource)
		{
			const D3D_SHADER_INPUT_TYPE inputType = static_cast<D3D_SHADER_INPUT_TYPE>(resource.type);
			if (inputType == D3D_SIT_SAMPLER)
			{
				return std::any_of(renderPass.samplers.begin(), renderPass.samplers.end(), [&](const auto& sampler)
				{
					return sampler.shaderRegister == resource.bindPoint &&
						sampler.registerSpace == resource.registerSpace;
				});
			}
			if (inputType == D3D_SIT_TEXTURE)
			{
				return std::any_of(renderPass.shaderResources.begin(), renderPass.shaderResources.end(), [&](const auto& injected)
				{
					return injected.shaderRegister == resource.bindPoint &&
						injected.registerSpace == resource.registerSpace;
				}) || std::any_of(renderPass.inputs.begin(), renderPass.inputs.end(), [&](const auto& input)
				{
					return input.origin == ShaderResource::ResourceOrigin::Runtime &&
						input.access == RenderPass::ResourceAccess::ShaderResource &&
						input.shaderRegister == resource.bindPoint &&
						input.registerSpace == resource.registerSpace;
				});
			}
			const bool unorderedAccessResource =
				inputType == D3D_SIT_UAV_RWTYPED ||
				inputType == D3D_SIT_UAV_RWSTRUCTURED ||
				inputType == D3D_SIT_UAV_RWBYTEADDRESS ||
				inputType == D3D_SIT_UAV_APPEND_STRUCTURED ||
				inputType == D3D_SIT_UAV_CONSUME_STRUCTURED ||
				inputType == D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER;
			if (!unorderedAccessResource)
				return false;
			return std::any_of(renderPass.outputs.begin(), renderPass.outputs.end(), [&](const auto& output)
			{
				return output.origin == ShaderResource::ResourceOrigin::Runtime &&
					output.access == RenderPass::ResourceAccess::UnorderedAccess &&
					output.shaderRegister == resource.bindPoint &&
					output.registerSpace == resource.registerSpace;
			});
		}

		bool ShouldDeclareInheritedResource(
			const RenderPass::RenderPassDisk& renderPass,
			const ShaderAnalysis::ResourceBindingDisk& resource)
		{
			return static_cast<D3D_SHADER_INPUT_TYPE>(resource.type) == D3D_SIT_CBUFFER
				? renderPass.inheritedGameBindings.constantBuffers
				: renderPass.inheritedGameBindings.shaderResources;
		}

		std::string SignatureValueType(const ShaderAnalysis::SignatureParameterDisk& parameter)
		{
			const char* scalarType = "float";
			if (parameter.componentType == D3D_REGISTER_COMPONENT_UINT32)
				scalarType = "uint";
			else if (parameter.componentType == D3D_REGISTER_COMPONENT_SINT32)
				scalarType = "int";
			uint32_t componentCount = 0;
			for (uint32_t mask = parameter.mask & 0xfu; mask; mask >>= 1)
				componentCount += mask & 1u;
			return componentCount > 1 ? std::string(scalarType) + std::to_string(componentCount) : scalarType;
		}

		void AppendSignatureStruct(
			std::ostringstream& source,
			const char* structName,
			const std::vector<ShaderAnalysis::SignatureParameterDisk>& parameters)
		{
			source << "struct " << structName << "\n{\n";
			for (size_t index = 0; index < parameters.size(); ++index)
			{
				const auto& parameter = parameters[index];
				const std::string semantic = parameter.semanticName.empty() ? "TEXCOORD" : parameter.semanticName;
				source << "\t" << SignatureValueType(parameter) << " value" << index << " : "
					<< semantic;
				if (parameter.semanticIndex)
					source << parameter.semanticIndex;
				source << ";\n";
			}
			source << "};\n\n";
		}

		std::string FirstRuntimeInputIdentifier(const RenderPass::RenderPassDisk& renderPass)
		{
			for (size_t inputIndex = 0; inputIndex < renderPass.inputs.size(); ++inputIndex)
			{
				const RenderPass::LogicalResourceBindingDisk& input = renderPass.inputs[inputIndex];
				if (input.origin == ShaderResource::ResourceOrigin::Runtime &&
					input.access == RenderPass::ResourceAccess::ShaderResource)
				{
					return SanitizeIdentifier(
						input.hlslName,
						"RuntimeInput_" + std::to_string(inputIndex));
				}
			}
			return {};
		}

		std::string FirstRuntimeOutputIdentifier(const RenderPass::RenderPassDisk& renderPass)
		{
			for (size_t outputIndex = 0; outputIndex < renderPass.outputs.size(); ++outputIndex)
			{
				const RenderPass::LogicalResourceBindingDisk& output = renderPass.outputs[outputIndex];
				if (output.origin == ShaderResource::ResourceOrigin::Runtime &&
					output.access == RenderPass::ResourceAccess::UnorderedAccess)
				{
					return SanitizeIdentifier(
						output.hlslName,
						"RuntimeOutput_" + std::to_string(outputIndex));
				}
			}
			return {};
		}

		std::string BuildFragmentShaderSource(
			const RenderPass::RenderPassDisk& renderPass,
			const ModifiedShader::PackageDisk& modifiedShader,
			bool replacementPixelShader)
		{
			std::ostringstream source;
			source << "// Fullscreen fragment shader for Modified Shader: "
				<< (modifiedShader.name.empty() ? modifiedShader.id : modifiedShader.name) << "\n"
				<< "// Enabled inherited bindings use the values from the linked game draw.\n"
				<< "// Reflected declarations use raw cbuffer storage where original HLSL types are unavailable.\n\n";

			const ShaderAnalysis::ShaderAnalysisDisk* analysis = SelectReflectionAnalysis(modifiedShader);
			std::unordered_set<std::string> usedIdentifiers;
			if (analysis)
			{
				for (const ShaderAnalysis::ResourceBindingDisk& resource : analysis->resourceBindings)
					if (ShouldDeclareInheritedResource(renderPass, resource) &&
						!IsInjectedTextureBinding(renderPass, resource))
						AppendResourceDeclaration(source, *analysis, resource, usedIdentifiers);
			}
			AppendInjectedResourceDeclarations(source, renderPass, usedIdentifiers);
			if (replacementPixelShader)
			{
				if (analysis && !analysis->inputParameters.empty() && !analysis->outputParameters.empty())
				{
					AppendSignatureStruct(source, "ReplacementPixelInput", analysis->inputParameters);
					AppendSignatureStruct(source, "ReplacementPixelOutput", analysis->outputParameters);
					source << "ReplacementPixelOutput main(ReplacementPixelInput input)\n{\n"
						<< "\tReplacementPixelOutput output = (ReplacementPixelOutput)0;\n"
						<< "\t// Implement the replacement pixel shader while retaining this reflected signature.\n"
						<< "\treturn output;\n}\n";
				}
				else
				{
					source << "// Reflection was unavailable. Update this signature to match the original pixel shader.\n"
						<< "float4 main(float4 position : SV_Position) : SV_Target0\n{\n"
						<< "\treturn float4(1.0, 0.0, 1.0, 1.0);\n}\n";
				}
				return source.str();
			}
			else
			{
				source << "// No successful reflection data was stored in this Modified Shader package.\n\n";
			}

			source << R"(struct FullscreenVertexOutput
{
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
};
)";
			const RenderPass::PassOperation operation = RenderPass::ResolvePassOperation(renderPass);
			const std::string runtimeInput = FirstRuntimeInputIdentifier(renderPass);
			if ((operation == RenderPass::PassOperation::Downsample ||
				operation == RenderPass::PassOperation::UpsampleChain) && !runtimeInput.empty())
			{
				source << "\nfloat4 main(FullscreenVertexOutput input) : SV_Target0\n{\n"
					<< "\tuint sourceWidth, sourceHeight;\n"
					<< "\t" << runtimeInput << ".GetDimensions(sourceWidth, sourceHeight);\n"
					<< "\tfloat2 sourcePosition = input.uv * float2(sourceWidth, sourceHeight) - 0.5;\n"
					<< "\tint2 sourceBase = int2(floor(sourcePosition));\n"
					<< "\tfloat2 sourceFraction = frac(sourcePosition);\n"
					<< "\tint2 maximumPixel = int2(max(sourceWidth, 1u) - 1u, max(sourceHeight, 1u) - 1u);\n"
					<< "\tint2 p00 = clamp(sourceBase, int2(0, 0), maximumPixel);\n"
					<< "\tint2 p10 = clamp(sourceBase + int2(1, 0), int2(0, 0), maximumPixel);\n"
					<< "\tint2 p01 = clamp(sourceBase + int2(0, 1), int2(0, 0), maximumPixel);\n"
					<< "\tint2 p11 = clamp(sourceBase + int2(1, 1), int2(0, 0), maximumPixel);\n"
					<< "\tfloat4 c00 = " << runtimeInput << ".Load(int3(p00, 0));\n"
					<< "\tfloat4 c10 = " << runtimeInput << ".Load(int3(p10, 0));\n"
					<< "\tfloat4 c01 = " << runtimeInput << ".Load(int3(p01, 0));\n"
					<< "\tfloat4 c11 = " << runtimeInput << ".Load(int3(p11, 0));\n";
				if (operation == RenderPass::PassOperation::Downsample)
					source << "\treturn (c00 + c10 + c01 + c11) * 0.25;\n";
				else
					source << "\treturn lerp(lerp(c00, c10, sourceFraction.x), lerp(c01, c11, sourceFraction.x), sourceFraction.y);\n";
				source << "}\n";
			}
			else
			{
				source << R"(
float4 main(FullscreenVertexOutput input) : SV_Target0
{
	// This template is intentionally a no-op until the pass author writes an effect.
	// The declarations above expose the resources bound by the linked game shader.
	discard;
	return float4(input.uv, 0.0, 1.0);
}
)";
			}
			return source.str();
		}

		std::string BuildComputeShaderSource(
			const RenderPass::RenderPassDisk& renderPass,
			const ModifiedShader::PackageDisk& modifiedShader)
		{
			std::ostringstream source;
			const bool replacement = renderPass.type == RenderPass::RenderPassType::ReplacementComputeShader;
			source << (replacement ? "// Replacement" : "// Custom Render Pass")
				<< " compute shader for Modified Shader: "
				<< (modifiedShader.name.empty() ? modifiedShader.id : modifiedShader.name) << "\n"
				<< "// Enabled inherited bindings use the values from the linked game dispatch.\n\n";
			std::unordered_set<std::string> usedIdentifiers;
			const ShaderAnalysis::ShaderAnalysisDisk* analysis = SelectReflectionAnalysis(modifiedShader);
			if (analysis)
			{
				for (const ShaderAnalysis::ResourceBindingDisk& resource : analysis->resourceBindings)
					if (ShouldDeclareInheritedResource(renderPass, resource) &&
						!IsInjectedTextureBinding(renderPass, resource))
						AppendResourceDeclaration(source, *analysis, resource, usedIdentifiers);
			}
			AppendInjectedResourceDeclarations(source, renderPass, usedIdentifiers);
			const uint32_t threadCountX = (std::max)(1u, renderPass.dispatch.threadGroupSizeX);
			const uint32_t threadCountY = (std::max)(1u, renderPass.dispatch.threadGroupSizeY);
			const uint32_t threadCountZ = (std::max)(1u, renderPass.dispatch.threadGroupSizeZ);
			const RenderPass::PassOperation operation = RenderPass::ResolvePassOperation(renderPass);
			const std::string runtimeInput = FirstRuntimeInputIdentifier(renderPass);
			const std::string runtimeOutput = FirstRuntimeOutputIdentifier(renderPass);
			source << "[numthreads(" << threadCountX << ", " << threadCountY << ", " << threadCountZ
				<< ")]\nvoid main(uint3 dispatchThreadId : SV_DispatchThreadID)\n{\n";
			if ((operation == RenderPass::PassOperation::Downsample ||
				operation == RenderPass::PassOperation::UpsampleChain) &&
				!runtimeInput.empty() && !runtimeOutput.empty())
			{
				source << "\tuint destinationWidth, destinationHeight;\n"
					<< "\t" << runtimeOutput << ".GetDimensions(destinationWidth, destinationHeight);\n"
					<< "\tif (dispatchThreadId.x >= destinationWidth || dispatchThreadId.y >= destinationHeight)\n"
					<< "\t\treturn;\n"
					<< "\tuint sourceWidth, sourceHeight;\n"
					<< "\t" << runtimeInput << ".GetDimensions(sourceWidth, sourceHeight);\n"
					<< "\tfloat2 sourcePosition = ((float2(dispatchThreadId.xy) + 0.5) / "
						"float2(destinationWidth, destinationHeight)) * float2(sourceWidth, sourceHeight) - 0.5;\n"
					<< "\tint2 sourceBase = int2(floor(sourcePosition));\n"
					<< "\tfloat2 sourceFraction = frac(sourcePosition);\n"
					<< "\tint2 maximumPixel = int2(max(sourceWidth, 1u) - 1u, max(sourceHeight, 1u) - 1u);\n"
					<< "\tint2 p00 = clamp(sourceBase, int2(0, 0), maximumPixel);\n"
					<< "\tint2 p10 = clamp(sourceBase + int2(1, 0), int2(0, 0), maximumPixel);\n"
					<< "\tint2 p01 = clamp(sourceBase + int2(0, 1), int2(0, 0), maximumPixel);\n"
					<< "\tint2 p11 = clamp(sourceBase + int2(1, 1), int2(0, 0), maximumPixel);\n"
					<< "\tfloat4 c00 = " << runtimeInput << ".Load(int3(p00, 0));\n"
					<< "\tfloat4 c10 = " << runtimeInput << ".Load(int3(p10, 0));\n"
					<< "\tfloat4 c01 = " << runtimeInput << ".Load(int3(p01, 0));\n"
					<< "\tfloat4 c11 = " << runtimeInput << ".Load(int3(p11, 0));\n";
				if (operation == RenderPass::PassOperation::Downsample)
					source << "\t" << runtimeOutput << "[dispatchThreadId.xy] = (c00 + c10 + c01 + c11) * 0.25;\n";
				else
					source << "\t" << runtimeOutput << "[dispatchThreadId.xy] = "
						"lerp(lerp(c00, c10, sourceFraction.x), lerp(c01, c11, sourceFraction.x), sourceFraction.y);\n";
			}
			else
			{
				source << (replacement
					? "\t// Implement the replacement compute workload here.\n"
					: "\t// Implement the injected compute workload here. Runtime UAV outputs are declared above.\n");
			}
			source << "}\n";
			return source.str();
		}

	}

	bool CreateShaderTemplate(
		RenderPass::RenderPassDisk& renderPass,
		const ModifiedShader::PackageDisk& modifiedShader,
		std::string& outError)
	{
		outError.clear();
		const bool computeReplacement = renderPass.type == RenderPass::RenderPassType::ReplacementComputeShader;
		const bool pixelReplacement = renderPass.type == RenderPass::RenderPassType::ReplacementPixelShader;
		const bool computePass = RenderPass::ResolveExecutionMode(renderPass) == RenderPass::ExecutionMode::Compute;
		if (computePass && modifiedShader.shaderType != ShaderTarget::ComputeShader)
		{
			outError = "Compute Render Passes require a compute Modified Shader.";
			return false;
		}
		if (!computePass && modifiedShader.shaderType != ShaderTarget::PixelShader)
		{
			outError = "Fullscreen fragment Render Passes require a pixel Modified Shader.";
			return false;
		}
		if (renderPass.packageDirectory.empty())
		{
			outError = "Render Pass package directory is unavailable.";
			return false;
		}

		const bool mipChain = renderPass.type == RenderPass::RenderPassType::MipChain;
		const RenderPass::PassOperation operation = RenderPass::ResolvePassOperation(renderPass);
		const bool downsample = operation == RenderPass::PassOperation::Downsample;
		const bool upsampleChain = operation == RenderPass::PassOperation::UpsampleChain;
		if (computePass)
		{
			if (mipChain)
			{
				renderPass.dispatch.threadGroupSizeX = 8;
				renderPass.dispatch.threadGroupSizeY = 8;
				renderPass.dispatch.threadGroupSizeZ = 1;
			}
			else if (const ShaderAnalysis::ShaderAnalysisDisk* analysis = SelectReflectionAnalysis(modifiedShader))
			{
				renderPass.dispatch.threadGroupSizeX = (std::max)(1u, analysis->executionProperties.threadGroupSizeX);
				renderPass.dispatch.threadGroupSizeY = (std::max)(1u, analysis->executionProperties.threadGroupSizeY);
				renderPass.dispatch.threadGroupSizeZ = (std::max)(1u, analysis->executionProperties.threadGroupSizeZ);
			}
		}
		renderPass.vertexShaderSourceFile = computePass || pixelReplacement ? std::string() :
			(mipChain ? mipChainVertexSourceFile : vertexSourceFile);
		renderPass.fragmentShaderSourceFile = computeReplacement ? replacementComputeSourceFile :
			(computePass ? (mipChain ? mipChainComputeSourceFile :
			(downsample ? downsampleComputeSourceFile :
			(upsampleChain ? upsampleComputeSourceFile : computeSourceFile))) :
			(pixelReplacement ? replacementPixelSourceFile : (mipChain ? mipChainFragmentSourceFile :
			(downsample ? downsampleFragmentSourceFile :
			(upsampleChain ? upsampleFragmentSourceFile : fragmentSourceFile)))));
		renderPass.vertexShaderCompiledBlobFile = computePass || pixelReplacement ? std::string() :
			(mipChain ? mipChainVertexBlobFile : vertexBlobFile);
		renderPass.fragmentShaderCompiledBlobFile = computeReplacement ? replacementComputeBlobFile :
			(computePass ? (mipChain ? mipChainComputeBlobFile :
			(downsample ? downsampleComputeBlobFile :
			(upsampleChain ? upsampleComputeBlobFile : computeBlobFile))) :
			(pixelReplacement ? replacementPixelBlobFile : (mipChain ? mipChainFragmentBlobFile :
			(downsample ? downsampleFragmentBlobFile :
			(upsampleChain ? upsampleFragmentBlobFile : fragmentBlobFile)))));
		renderPass.fragmentShaderProfile = StringHelper::ShaderProfileForType(
			computePass ? ShaderTarget::ComputeShader : ShaderTarget::PixelShader);
		renderPass.vertexShaderProfile = StringHelper::ShaderProfileForType(ShaderTarget::VertexShader);
		renderPass.vertexShaderEntryPoint = "main";
		renderPass.fragmentShaderEntryPoint = "main";
		RenderPass::ResolveShaderPaths(renderPass);

		if (!renderPass.vertexShaderSourcePath.empty() && !ShaderInjectorIO::WriteTextFileIfMissing(
			renderPass.vertexShaderSourcePath,
			fullscreenTriangleVertexShader))
		{
			outError = "Could not create the fullscreen vertex shader source.";
			return false;
		}
		if (!ShaderInjectorIO::WriteTextFileIfMissing(
			renderPass.fragmentShaderSourcePath,
			computePass ? (mipChain ? mipChainComputeShader : BuildComputeShaderSource(renderPass, modifiedShader)) :
			(mipChain ? mipChainFragmentShader : BuildFragmentShaderSource(renderPass, modifiedShader, pixelReplacement))))
		{
			outError = "Could not create the Render Pass shader source.";
			return false;
		}

		return CompileShaders(renderPass, outError);
	}

	bool CompileShaders(RenderPass::RenderPassDisk& renderPass, std::string& outError)
	{
		outError.clear();
		RenderPass::ResolveShaderPaths(renderPass);
		if (!RenderPass::HasShaderTemplate(renderPass))
		{
			outError = "Render Pass shader source files are missing.";
			return false;
		}

		const bool computePass = RenderPass::ResolveExecutionMode(renderPass) == RenderPass::ExecutionMode::Compute;
		renderPass.fragmentShaderProfile = StringHelper::ShaderProfileForType(
			computePass ? ShaderTarget::ComputeShader : ShaderTarget::PixelShader);
		renderPass.vertexShaderProfile = StringHelper::ShaderProfileForType(ShaderTarget::VertexShader);

		std::string vertexBlobPath = renderPass.vertexShaderCompiledBlobPath;
		if (!vertexBlobPath.empty() && !ShaderInjectorIO::CompileSourceToDXILBlob(
			renderPass.vertexShaderSourcePath,
			renderPass.vertexShaderProfile,
			renderPass.vertexShaderEntryPoint,
			vertexBlobPath))
		{
			outError = "Fullscreen vertex shader compilation failed.";
			return false;
		}

		std::string fragmentBlobPath = renderPass.fragmentShaderCompiledBlobPath;
		if (!ShaderInjectorIO::CompileSourceToDXILBlob(
			renderPass.fragmentShaderSourcePath,
			renderPass.fragmentShaderProfile,
			renderPass.fragmentShaderEntryPoint,
			fragmentBlobPath))
		{
			outError = "Render Pass shader compilation failed.";
			return false;
		}

		if (!RenderPass::LoadCompiledShaderBlobs(renderPass))
		{
			outError = "Compiled Render Pass shaders could not be loaded.";
			return false;
		}
		return true;
	}
}
