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
	//identify the shader or pass event that triggers this pass.
	struct EventReferenceDisk
	{
		EventType type = EventType::ModifiedShader;
		std::string id;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			EventReferenceDisk,
			type,
			id)
	};
} //namespace RenderPass
