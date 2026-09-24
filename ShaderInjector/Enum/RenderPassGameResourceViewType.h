#pragma once

#include "JsonHelper.h"

namespace RenderPass
{
	enum class GameResourceViewType
	{
		ShaderResource,
		UnorderedAccess,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(GameResourceViewType,
								 {
									 {GameResourceViewType::ShaderResource, "ShaderResource"},
									 {GameResourceViewType::UnorderedAccess, "UnorderedAccess"},
								 })
} //namespace RenderPass
