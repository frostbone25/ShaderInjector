#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <d3d12.h>
#include "JsonHelper.h"
#include "ShaderResource/ShaderResource.h"
#include "Enum/RenderPassRenderPassType.h"
#include "Enum/RenderPassExecutionMode.h"
#include "Enum/RenderPassPassOperation.h"
#include "Enum/RenderPassDispatchMode.h"
#include "Enum/RenderPassViewportMode.h"
#include "Enum/RenderPassResourceAccess.h"
#include "Enum/RenderPassGameResourceViewType.h"
#include "Enum/RenderPassEventType.h"

namespace RenderPass
{
	//describe a texture created by this pass and the earlier texture it may reuse.
	struct RuntimeResourceDefinitionDisk
	{
		std::string id;
		std::string name;
		//A later pass can write through this new logical ID into an earlier texture.
		std::string reuseFromResourceId;
		ShaderResource::TextureDescriptionDisk texture;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			RuntimeResourceDefinitionDisk,
			id,
			name,
			reuseFromResourceId,
			texture)
	};
} //namespace RenderPass
