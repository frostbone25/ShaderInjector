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
	//capture one observed binding so the GUI can explain the command list state.
	struct ResourceBindingDiagnostic
	{
		std::string pipeline;
		std::string bindingType;
		uint32_t rootParameterIndex = UINT32_MAX;
		uint64_t gpuAddress = 0;
		uint64_t gpuDescriptorHandle = 0;
		uint64_t cpuDescriptorHandle = 0;
		uint32_t descriptorHeapType = UINT32_MAX;
		uint32_t descriptorIndex = UINT32_MAX;
		uint32_t descriptorCount = 0;
		uint32_t descriptorViewDimension = UINT32_MAX;
		uint32_t descriptorMostDetailedMip = 0;
		uint32_t descriptorMipLevels = 0;
		uint32_t descriptorShader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		uint32_t descriptorPlaneSlice = 0;
		float descriptorResourceMinLodClamp = 0.0f;
		uint32_t shaderRegister = UINT32_MAX;
		uint32_t registerSpace = UINT32_MAX;
		uint32_t destinationOffset = 0;
		uint64_t resourcePointer = 0;
		std::string resourceName;
		uint32_t resourceDimension = UINT32_MAX;
		uint64_t resourceWidth = 0;
		uint32_t resourceHeight = 0;
		uint32_t resourceDepthOrArraySize = 0;
		uint32_t resourceMipLevels = 0;
		uint32_t resourceFormat = 0;
		uint32_t resourceSampleCount = 0;
		uint32_t resourceSampleQuality = 0;
		uint64_t bufferOffset = 0;
		uint64_t bufferSize = 0;
		uint64_t firstElement = 0;
		uint32_t elementCount = 0;
		uint32_t structureByteStride = 0;
		std::vector<uint32_t> rootConstants;
	};
} //namespace RenderPass
