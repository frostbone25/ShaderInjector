#pragma once

#include <cstdint>

namespace RenderPassRuntime
{
	enum TrackingModeFlag : uint32_t
	{
		TrackingEnabled = 1u << 0,
		ResourceTrackingEnabled = 1u << 1,
		DescriptorRegistryTrackingEnabled = 1u << 2,
		GraphicsStateTrackingEnabled = 1u << 3,
		DescriptorTableTrackingEnabled = 1u << 4,
		RootBindingTrackingEnabled = 1u << 5
	};
} //namespace RenderPassRuntime
