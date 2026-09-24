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
	//save the viewport rule that the pass applies before drawing.
	struct ViewportPolicyDisk
	{
		ViewportMode mode = ViewportMode::InheritOriginal;
		uint32_t width = 0;
		uint32_t height = 0;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			ViewportPolicyDisk,
			mode,
			width,
			height)
	};
} //namespace RenderPass
