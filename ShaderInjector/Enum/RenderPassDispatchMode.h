#pragma once

#include "JsonHelper.h"

namespace RenderPass
{
	enum class DispatchMode
	{
		InheritOriginal,
		ScaleByResolution,
		ExplicitThreadGroups,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(DispatchMode,
	{
		{ DispatchMode::InheritOriginal, "InheritOriginal" },
		{ DispatchMode::ScaleByResolution, "ScaleByResolution" },
		{ DispatchMode::ExplicitThreadGroups, "ExplicitThreadGroups" },
	})
}
