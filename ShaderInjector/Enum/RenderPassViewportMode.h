#pragma once

#include "JsonHelper.h"

namespace RenderPass
{
	enum class ViewportMode
	{
		InheritOriginal,
		MatchOutput,
		Explicit,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(ViewportMode,
								 {
									 {ViewportMode::InheritOriginal, "InheritOriginal"},
									 {ViewportMode::MatchOutput, "MatchOutput"},
									 {ViewportMode::Explicit, "Explicit"},
								 })
} //namespace RenderPass
