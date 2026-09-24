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
#include "RenderPass/ResourceBindingDiagnostic.h"

namespace RenderPass
{
	//collect recent execution outcomes and the bindings captured for inspection.
	struct RuntimeDiagnostics
	{
		uint64_t triggerCount = 0;
		uint64_t executionCount = 0;
		uint64_t executionFailureCount = 0;
		std::string lastTiming;
		std::string lastOperation;
		std::string lastExecutionError;
		std::string lastEventType;
		std::string lastEventId;
		std::string lastModifiedShaderId;
		std::string lastShaderTargetName;
		std::string lastShaderTargetHash;
		bool resourceSnapshotCaptured = false;
		std::vector<ResourceBindingDiagnostic> resourceBindings;
	};
} //namespace RenderPass
