#pragma once

#include "JsonHelper.h"

namespace RenderPass
{
	enum class RenderPassType
	{
		Custom,
		MipChain,
		DownsampleUpsample,
		TemporalHistory,
		ReplacementPixelShader,
		ReplacementComputeShader,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(RenderPassType,
								 {
									 {RenderPassType::Custom, "Custom"},
									 {RenderPassType::MipChain, "MipChain"},
						 {RenderPassType::DownsampleUpsample, "DownsampleUpsample"},
									 {RenderPassType::TemporalHistory, "TemporalHistory"},
									 {RenderPassType::ReplacementPixelShader, "ReplacementPixelShader"},
									 {RenderPassType::ReplacementComputeShader, "ReplacementComputeShader"},
								 })
} //namespace RenderPass
