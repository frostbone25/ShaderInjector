#pragma once

#include "JsonHelper.h"

namespace RenderPass
{
	enum class EventType
	{
		ModifiedShader,
		RenderPass,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(EventType,
	{
		{ EventType::ModifiedShader, "ModifiedShader" },
		{ EventType::RenderPass, "RenderPass" },
	})
}
