#include "HookD3D12RenderPass.h"

namespace HookD3D12
{
	namespace
	{
		thread_local unsigned int gRenderPassInjectionDepth = 0;
	}

	ScopedRenderPassInjection::ScopedRenderPassInjection()
	{
		++gRenderPassInjectionDepth;
	}

	ScopedRenderPassInjection::~ScopedRenderPassInjection()
	{
		if (gRenderPassInjectionDepth > 0)
			--gRenderPassInjectionDepth;
	}

	bool IsInsideRenderPassInjection()
	{
		return gRenderPassInjectionDepth != 0;
	}
}
