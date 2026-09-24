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
	//choose which bindings from the triggering game shader carry into this pass.
	struct InheritedGameBindingsDisk
	{
		//Preserve the resource contract of the Modified Shader that anchors this
		//pass. The two categories can be disabled independently for deliberately
		//self-contained passes.
		bool shaderResources = true;
		bool constantBuffers = true;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			InheritedGameBindingsDisk,
			shaderResources,
			constantBuffers)
	};
} //namespace RenderPass
