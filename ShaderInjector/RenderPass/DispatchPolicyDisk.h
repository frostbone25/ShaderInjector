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
	//save whether compute dispatch inherits the game count or uses explicit groups.
	struct DispatchPolicyDisk
	{
		DispatchMode mode = DispatchMode::InheritOriginal;
		uint32_t threadGroupSizeX = 8;
		uint32_t threadGroupSizeY = 8;
		uint32_t threadGroupSizeZ = 1;
		uint32_t explicitGroupCountX = 0;
		uint32_t explicitGroupCountY = 0;
		uint32_t explicitGroupCountZ = 0;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			DispatchPolicyDisk,
			mode,
			threadGroupSizeX,
			threadGroupSizeY,
			threadGroupSizeZ,
			explicitGroupCountX,
			explicitGroupCountY,
			explicitGroupCountZ)
	};
} //namespace RenderPass
