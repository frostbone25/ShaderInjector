#pragma once

namespace HookD3D12
{
	class ScopedRenderPassInjection
	{
	public:
		ScopedRenderPassInjection();
		~ScopedRenderPassInjection();

		ScopedRenderPassInjection(const ScopedRenderPassInjection&) = delete;
		ScopedRenderPassInjection& operator=(const ScopedRenderPassInjection&) = delete;
	};
}
