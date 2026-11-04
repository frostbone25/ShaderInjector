#pragma once

#include <vector>

#include "RenderPass/Registry/RegistryStatistics.h"
#include "RenderPass/TexturePoolPerformanceStatistics.h"
#include "RenderPass/Runtime/PassPerformanceStatistics.h"
#include "ShaderResource/Runtime/ShaderResourcePerformanceStatistics.h"

namespace RenderPassRuntime
{
	struct PerformanceSnapshot
	{
		bool available = false;
		RenderPassResourceRegistry::RegistryStatistics registry;
		RenderPassTexturePool::PerformanceStatistics textures;
		ShaderResourceRuntime::PerformanceStatistics shaderResources;
		std::vector<PassPerformanceStatistics> passes;
	};
}
