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
	//store the register mapping for a shader resource in a pass package.
	struct ShaderResourceReferenceDisk
	{
		std::string resourceId;
		std::string hlslName;
		uint32_t shaderRegister = 0;
		uint32_t registerSpace = 0;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			ShaderResourceReferenceDisk,
			resourceId,
			hlslName,
			shaderRegister,
			registerSpace)
	};
} //namespace RenderPass
