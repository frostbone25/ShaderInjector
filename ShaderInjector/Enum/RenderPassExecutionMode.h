#pragma once

#include "JsonHelper.h"

namespace RenderPass
{
	enum class ExecutionMode
	{
		Automatic,
		FullscreenPixel,
		Compute,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(ExecutionMode,
	{
		{ ExecutionMode::Automatic, "Automatic" },
		{ ExecutionMode::FullscreenPixel, "FullscreenPixel" },
		{ ExecutionMode::Compute, "Compute" },
	})
}
