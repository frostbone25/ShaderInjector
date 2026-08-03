#include "RenderPassShaders.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

#include <d3d12shader.h>

#include "ShaderInjectorIO.h"

namespace RenderPassShaders
{
	namespace
	{
		constexpr const char* vertexSourceFile = "FullscreenTriangleVS.hlsl";
		constexpr const char* fragmentSourceFile = "Fragment.hlsl";
		constexpr const char* vertexBlobFile = "FullscreenTriangleVS.blob";
		constexpr const char* fragmentBlobFile = "Fragment.blob";
		constexpr const char* mipChainVertexSourceFile = "MipChainVS.hlsl";
		constexpr const char* mipChainFragmentSourceFile = "MipChainDownsample.hlsl";
		constexpr const char* mipChainVertexBlobFile = "MipChainVS.blob";
		constexpr const char* mipChainFragmentBlobFile = "MipChainDownsample.blob";

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

		std::string BuildFragmentShaderSource(const ModifiedShader::PackageDisk& modifiedShader)
		{
			std::ostringstream source;
			source << "// Fullscreen fragment shader for Modified Shader: "
				<< (modifiedShader.name.empty() ? modifiedShader.id : modifiedShader.name) << "\n"
				<< "// Root bindings remain exactly as the linked game draw configured them.\n"
				<< "// Reflected declarations use raw cbuffer storage where original HLSL types are unavailable.\n\n";

			const ShaderAnalysis::ShaderAnalysisDisk* analysis = SelectReflectionAnalysis(modifiedShader);
			if (analysis)
			{
				std::unordered_set<std::string> usedIdentifiers;
				for (const ShaderAnalysis::ResourceBindingDisk& resource : analysis->resourceBindings)
					AppendResourceDeclaration(source, *analysis, resource, usedIdentifiers);
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

float4 main(FullscreenVertexOutput input) : SV_Target0
{
	// This template is intentionally a no-op until the pass author writes an effect.
	// The declarations above expose the resources bound by the linked game shader.
	discard;
	return float4(input.uv, 0.0, 1.0);
}
)";
			return source.str();
		}

		std::string VertexProfileFromPixelProfile(const std::string& pixelProfile)
		{
			if (pixelProfile.rfind("ps_", 0) == 0)
				return "vs_" + pixelProfile.substr(3);
			return "vs_6_6";
		}
	}

	bool CreateFragmentShaderTemplate(
		RenderPass::RenderPassDisk& renderPass,
		const ModifiedShader::PackageDisk& modifiedShader,
		std::string& outError)
	{
		outError.clear();
		if (modifiedShader.shaderType != ShaderTarget::PixelShader)
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
		renderPass.vertexShaderSourceFile = mipChain ? mipChainVertexSourceFile : vertexSourceFile;
		renderPass.fragmentShaderSourceFile = mipChain ? mipChainFragmentSourceFile : fragmentSourceFile;
		renderPass.vertexShaderCompiledBlobFile = mipChain ? mipChainVertexBlobFile : vertexBlobFile;
		renderPass.fragmentShaderCompiledBlobFile = mipChain ? mipChainFragmentBlobFile : fragmentBlobFile;
		renderPass.fragmentShaderProfile = modifiedShader.shaderProfile.empty()
			? "ps_6_6"
			: modifiedShader.shaderProfile;
		renderPass.vertexShaderProfile = VertexProfileFromPixelProfile(renderPass.fragmentShaderProfile);
		renderPass.vertexShaderEntryPoint = "main";
		renderPass.fragmentShaderEntryPoint = "main";
		RenderPass::ResolveShaderPaths(renderPass);

		if (!ShaderInjectorIO::WriteTextFileIfMissing(
			renderPass.vertexShaderSourcePath,
			fullscreenTriangleVertexShader))
		{
			outError = "Could not create the fullscreen vertex shader source.";
			return false;
		}
		if (!ShaderInjectorIO::WriteTextFileIfMissing(
			renderPass.fragmentShaderSourcePath,
			mipChain ? mipChainFragmentShader : BuildFragmentShaderSource(modifiedShader)))
		{
			outError = "Could not create the fragment shader source.";
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

		std::string vertexBlobPath = renderPass.vertexShaderCompiledBlobPath;
		if (!ShaderInjectorIO::CompileSourceToDXILBlob(
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
			outError = "Fragment shader compilation failed.";
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
