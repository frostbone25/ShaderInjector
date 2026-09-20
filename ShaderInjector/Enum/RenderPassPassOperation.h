#pragma once

#include "JsonHelper.h"

namespace RenderPass
{
	enum class PassOperation
	{
		Automatic,
		Custom,
		ReplaceOriginal,
		MipChain,
		Downsample,
		UpsampleChain,
		Copy,
		TemporalHistory,
		Resolve,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(PassOperation,
	{
		{ PassOperation::Automatic, "Automatic" },
		{ PassOperation::Custom, "Custom" },
		{ PassOperation::ReplaceOriginal, "ReplaceOriginal" },
		{ PassOperation::MipChain, "MipChain" },
		{ PassOperation::Downsample, "Downsample" },
		{ PassOperation::UpsampleChain, "UpsampleChain" },
		{ PassOperation::Copy, "Copy" },
		{ PassOperation::TemporalHistory, "TemporalHistory" },
		{ PassOperation::Resolve, "Resolve" },
	})
}
