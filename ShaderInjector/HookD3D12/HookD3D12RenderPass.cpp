#include "HookD3D12.h"

namespace HookD3D12
{
	thread_local unsigned int gRenderPassInjectionDepth = 0;

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
} //namespace HookD3D12
