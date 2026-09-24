#pragma once

#include <string>

#include "RenderPass/RenderPass.h"

namespace RenderPassTexturePool
{
	//keep each runtime texture definition with the pass that owns it.
	struct DefinitionRecord
	{
		std::string ownerRenderPassId;
		RenderPass::RuntimeResourceDefinitionDisk definition;
	};
} //namespace RenderPassTexturePool
