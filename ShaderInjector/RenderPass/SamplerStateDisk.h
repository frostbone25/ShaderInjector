#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <d3d12.h>
#include "JsonHelper.h"
#include "ShaderResource/ShaderResource.h"
#include "Enum/RenderPassRenderPassType.h"
#include "Enum/RenderPassExecutionMode.h"
#include "Enum/RenderPassPassOperation.h"
#include "Enum/RenderPassDispatchMode.h"
#include "Enum/RenderPassViewportMode.h"
#include "Enum/RenderPassResourceAccess.h"
#include "Enum/RenderPassGameResourceViewType.h"
#include "Enum/RenderPassEventType.h"

namespace RenderPass
{
	//persist the sampler settings that will become a D3D12 static sampler.
	struct SamplerStateDisk
	{
		std::string hlslName = "SI_Sampler";
		uint32_t shaderRegister = 0;
		uint32_t registerSpace = 0;
		uint32_t filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		uint32_t addressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		uint32_t addressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		uint32_t addressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		float mipLodBias = 0.0f;
		uint32_t maximumAnisotropy = 1;
		uint32_t comparisonFunction = D3D12_COMPARISON_FUNC_ALWAYS;
		std::array<float, 4> borderColor{0.0f, 0.0f, 0.0f, 0.0f};
		float minimumLod = 0.0f;
		float maximumLod = D3D12_FLOAT32_MAX;
		bool comparisonSampler = false;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			SamplerStateDisk,
			hlslName,
			shaderRegister,
			registerSpace,
			filter,
			addressU,
			addressV,
			addressW,
			mipLodBias,
			maximumAnisotropy,
			comparisonFunction,
			borderColor,
			minimumLod,
			maximumLod,
			comparisonSampler)
	};
} //namespace RenderPass
