#include "Enum/EnumStrings.h"
#include "Enum/RenderDocAvailability.h"
#include "Globals.h"
#include "RenderPass/RenderPass.h"
#include "ShaderResource/ShaderResource.h"
#include "ShaderTarget/ShaderTarget.h"
#include "StringHelper.h"

namespace RenderPassRuntime
{
	const char* RootBindingTypeName(RootBindingType type)
	{
		switch (type)
		{
			case RootBindingType::DescriptorTable: return "Descriptor Table";
			case RootBindingType::ConstantBufferView: return "CBV";
			case RootBindingType::ShaderResourceView: return "SRV";
			case RootBindingType::UnorderedAccessView: return "UAV";
			case RootBindingType::Constants: return "Root Constants";
			default: return "";
		}
	}
}

std::string GetProcessorArchitectureName(WORD architecture)
{
	switch (architecture)
	{
		case PROCESSOR_ARCHITECTURE_AMD64: 
			return "x64";
		case PROCESSOR_ARCHITECTURE_ARM: 
			return "ARM";
		case PROCESSOR_ARCHITECTURE_ARM64: 
			return "ARM64";
		case PROCESSOR_ARCHITECTURE_INTEL: 
			return "x86";
		default: 
			return "Unknown";
	}
}

std::string D3DFeatureLevelToString(D3D_FEATURE_LEVEL featureLevel)
{
	switch (featureLevel)
	{
		case D3D_FEATURE_LEVEL_12_2: 
			return "12_2";
		case D3D_FEATURE_LEVEL_12_1: 
			return "12_1";
		case D3D_FEATURE_LEVEL_12_0: 
			return "12_0";
		case D3D_FEATURE_LEVEL_11_1: 
			return "11_1";
		case D3D_FEATURE_LEVEL_11_0: 
			return "11_0";
		default: 
			return std::to_string((UINT)featureLevel);
	}
}

std::string D3DShaderModelToString(D3D_SHADER_MODEL shaderModel)
{
	switch (shaderModel)
	{
		case D3D_SHADER_MODEL_6_7:
			return "6_7";
		case D3D_SHADER_MODEL_6_6: 
			return "6_6";
		case D3D_SHADER_MODEL_6_5: 
			return "6_5";
		case D3D_SHADER_MODEL_6_4: 
			return "6_4";
		case D3D_SHADER_MODEL_6_3: 
			return "6_3";
		case D3D_SHADER_MODEL_6_2: 
			return "6_2";
		case D3D_SHADER_MODEL_6_1:
			return "6_1";
		case D3D_SHADER_MODEL_6_0: 
			return "6_0";
		default: 
			return std::to_string((UINT)shaderModel);
	}
}

const char* RenderDocAvailabilityText(RenderDocAvailability availability)
{
	switch (availability)
	{
		case RenderDocAvailability::Disabled: 
			return "Disabled";
		case RenderDocAvailability::NotAttached: 
			return "Not attached";
		case RenderDocAvailability::InstallationNotFound: 
			return "RenderDoc installation was not found";
		case RenderDocAvailability::ModuleLoadFailed:
			return "RenderDoc library failed to load";
		case RenderDocAvailability::ApiEntryPointMissing: 
			return "RENDERDOC_GetAPI is unavailable";
		case RenderDocAvailability::ApiVersionUnsupported: 
			return "RenderDoc API version is unsupported";
		case RenderDocAvailability::Ready: 
			return "Ready";
		default: 
			return "Unavailable";
	}
}

namespace StringHelper
{
	std::string ShaderTypeToString(ShaderTarget::ShaderType shaderType)
	{
		switch (shaderType)
		{
			case ShaderTarget::VertexShader: 
				return "VertexShader";
			case ShaderTarget::HullShader: 
				return "HullShader";
			case ShaderTarget::DomainShader: 
				return "DomainShader";
			case ShaderTarget::GeometryShader: 
				return "GeometryShader";
			case ShaderTarget::PixelShader: 
				return "PixelShader";
			case ShaderTarget::ComputeShader: 
				return "ComputeShader";
			default: 
				return "Unknown";
		}
	}

	std::string ShaderModelToString(Globals::ShaderModel shaderModel)
	{
		switch (shaderModel)
		{
			case Globals::ShaderModel::ShaderModel5_0: 
				return "5_0";
			case Globals::ShaderModel::ShaderModel5_1: 
				return "5_1";
			case Globals::ShaderModel::ShaderModel6_0: 
				return "6_0";
			case Globals::ShaderModel::ShaderModel6_1: 
				return "6_1";
			case Globals::ShaderModel::ShaderModel6_2: 
				return "6_2";
			case Globals::ShaderModel::ShaderModel6_3: 
				return "6_3";
			case Globals::ShaderModel::ShaderModel6_4: 
				return "6_4";
			case Globals::ShaderModel::ShaderModel6_5: 
				return "6_5";
			case Globals::ShaderModel::ShaderModel6_6: 
				return "6_6";
			default: 
				return "6_6";
		}
	}
}

namespace RenderPass
{
	const char* TypeName(RenderPassType type)
	{
		switch (type)
		{
			case RenderPassType::MipChain: return "MipChain";
			case RenderPassType::TemporalHistory: return "Temporal History";
			case RenderPassType::ReplacementPixelShader: return "Replacement Pixel Shader";
			case RenderPassType::ReplacementComputeShader: return "Replacement Compute Shader";
			case RenderPassType::Custom:
			default: return "Custom";
		}
	}

	const char* ExecutionModeName(ExecutionMode mode)
	{
		switch (mode)
		{
			case ExecutionMode::FullscreenPixel: return "Fullscreen Pixel";
			case ExecutionMode::Compute: return "Compute";
			case ExecutionMode::Automatic:
			default: return "Automatic";
		}
	}

	const char* PassOperationName(PassOperation operation)
	{
		switch (operation)
		{
			case PassOperation::Custom: return "Custom";
			case PassOperation::ReplaceOriginal: return "Replace Original";
			case PassOperation::MipChain: return "Mip Chain";
			case PassOperation::Downsample: return "Downsample";
			case PassOperation::UpsampleChain: return "Upsample Chain";
			case PassOperation::Copy: return "Copy";
			case PassOperation::TemporalHistory: return "Temporal History Copy";
			case PassOperation::Resolve: return "Resolve";
			case PassOperation::Automatic:
			default: return "Automatic";
		}
	}

	const char* EventTypeName(EventType type)
	{
		switch (type)
		{
			case EventType::RenderPass: return "Render Pass";
			case EventType::ModifiedShader:
			default: return "Modified Shader";
		}
	}
}

namespace ShaderResource
{
	const char* ResourceOriginName(ResourceOrigin origin)
	{
		switch (origin)
		{
			case ResourceOrigin::Game: return "Game Runtime";
			case ResourceOrigin::Runtime: return "Injector Runtime";
			case ResourceOrigin::Disk:
			default: return "Offline / Disk";
		}
	}

	const char* ResourceLifetimeName(ResourceLifetime lifetime)
	{
		switch (lifetime)
		{
			case ResourceLifetime::Transient: return "Transient";
			case ResourceLifetime::Persistent: return "Persistent";
			case ResourceLifetime::History: return "History";
			case ResourceLifetime::Immutable:
			default: return "Immutable";
		}
	}

	const char* TextureDimensionName(TextureDimension dimension)
	{
		switch (dimension)
		{
			case TextureDimension::Texture2D: return "Texture2D";
			case TextureDimension::Texture2DArray: return "Texture2DArray";
			case TextureDimension::TextureCube: return "TextureCube";
			case TextureDimension::TextureCubeArray: return "TextureCubeArray";
			case TextureDimension::Texture3D: return "Texture3D";
			case TextureDimension::Unknown:
			default: return "Unknown";
		}
	}

	const char* TextureHlslTypeName(TextureDimension dimension)
	{
		switch (dimension)
		{
			case TextureDimension::Texture2DArray: return "Texture2DArray<float4>";
			case TextureDimension::TextureCube: return "TextureCube<float4>";
			case TextureDimension::TextureCubeArray: return "TextureCubeArray<float4>";
			case TextureDimension::Texture3D: return "Texture3D<float4>";
			case TextureDimension::Texture2D:
			case TextureDimension::Unknown:
			default: return "Texture2D<float4>";
		}
	}
}
