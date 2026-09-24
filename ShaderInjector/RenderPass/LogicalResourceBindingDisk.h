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
	//connect a logical texture name to the register and access a shader expects.
	struct LogicalResourceBindingDisk
	{
		std::string resourceId;
		std::string hlslName;
		ShaderResource::ResourceOrigin origin = ShaderResource::ResourceOrigin::Runtime;
		ResourceAccess access = ResourceAccess::ShaderResource;
		GameResourceViewType gameResourceViewType = GameResourceViewType::UnorderedAccess;
		ShaderResource::TemporalView temporalView = ShaderResource::TemporalView::Current;
		uint32_t shaderRegister = 0;
		uint32_t registerSpace = 0;
		bool optional = false;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			LogicalResourceBindingDisk,
			resourceId,
			hlslName,
			origin,
			access,
			gameResourceViewType,
			temporalView,
			shaderRegister,
			registerSpace,
			optional)
	};
} //namespace RenderPass
