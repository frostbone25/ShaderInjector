#pragma once

#include "JsonHelper.h"

namespace RenderPass
{
	enum class RenderPassType
	{
		Custom,
		MipChain,
		TemporalHistory,
		ReplacementPixelShader,
		ReplacementComputeShader,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(RenderPassType,
								 {
									 {RenderPassType::Custom, "Custom"},
									 {RenderPassType::MipChain, "MipChain"},
									 {RenderPassType::TemporalHistory, "TemporalHistory"},
									 {RenderPassType::ReplacementPixelShader, "ReplacementPixelShader"},
									 {RenderPassType::ReplacementComputeShader, "ReplacementComputeShader"},
								 })
} //namespace RenderPass
