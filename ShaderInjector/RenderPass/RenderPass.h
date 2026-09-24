#pragma once
#include "Enum/RenderPassRenderPassType.h"
#include "Enum/RenderPassExecutionMode.h"
#include "Enum/RenderPassPassOperation.h"
#include "Enum/RenderPassDispatchMode.h"
#include "Enum/RenderPassViewportMode.h"
#include "Enum/RenderPassResourceAccess.h"
#include "Enum/RenderPassGameResourceViewType.h"
#include "Enum/RenderPassEventType.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>

#include "JsonHelper.h"
#include "RenderPass/RenderPassSchema.h"
#include "RenderPass/DispatchPolicyDisk.h"
#include "RenderPass/ViewportPolicyDisk.h"
#include "RenderPass/LogicalResourceBindingDisk.h"
#include "RenderPass/RuntimeResourceDefinitionDisk.h"
#include "RenderPass/ShaderResourceReferenceDisk.h"
#include "RenderPass/SamplerStateDisk.h"
#include "RenderPass/InheritedGameBindingsDisk.h"
#include "RenderPass/EventReferenceDisk.h"
#include "RenderPass/RenderPassDisk.h"
#include "RenderPass/ResourceBindingDiagnostic.h"
#include "RenderPass/RuntimeDiagnostics.h"
#include "ShaderResource/ShaderResource.h"
namespace RenderPass
{
	bool WriteJson(const RenderPassDisk& renderPass);
	bool LoadJson(const std::string& jsonPath, RenderPassDisk& outRenderPass);
	bool IsTimingValid(const std::string& timing);
	void ResolveShaderPaths(RenderPassDisk& renderPass);
	bool LoadCompiledShaderBlobs(RenderPassDisk& renderPass);
	const char* TypeName(RenderPassType type);
	const char* ExecutionModeName(ExecutionMode mode);
	const char* PassOperationName(PassOperation operation);
	const char* EventTypeName(EventType type);
	ExecutionMode ResolveExecutionMode(const RenderPassDisk& renderPass);
	PassOperation ResolvePassOperation(const RenderPassDisk& renderPass);
	const LogicalResourceBindingDisk* FindMipChainRuntimeSource(const RenderPassDisk& renderPass);
	std::string MipChainOutputResourceId(const RenderPassDisk& renderPass);
	std::string TemporalHistoryResourceId(const RenderPassDisk& renderPass);
	void ConfigureTemporalHistoryPass(RenderPassDisk& renderPass);
	void NormalizeExecutionResources(RenderPassDisk& renderPass);
	bool HasShaderTemplate(const RenderPassDisk& renderPass);
	bool HasCompiledShaders(const RenderPassDisk& renderPass);
	bool IsReplacementPass(RenderPassType type);
} //namespace RenderPass
