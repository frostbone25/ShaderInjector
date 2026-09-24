#pragma once

#include <string>

#include "Enum/ShaderResourceTemporalView.h"
#include "RenderPass/RenderPassTexturePool.h"

namespace RenderPassTexturePool
{
	//override one graph input for the current thread's execution scope.
	struct InputTextureOverride
	{
		std::string resourceId;
		ShaderResource::TemporalView temporalView;
		TextureView texture;
	};
} //namespace RenderPassTexturePool
