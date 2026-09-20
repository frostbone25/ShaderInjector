#pragma once

#include <cstdint>
#include "JsonHelper.h"

namespace ShaderResource
{
	enum class TemporalView : uint8_t
	{
		Current,
		Previous,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(TemporalView,
	{
		{ TemporalView::Current, "Current" },
		{ TemporalView::Previous, "Previous" },
	})
}
