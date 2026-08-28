#include "ShaderResource/ShaderResource.h"

namespace ShaderResource
{
	const char* ResourceOriginName(ResourceOrigin origin)
	{
		switch (origin)
		{
			case ResourceOrigin::Game: return "Game Runtime";
			case ResourceOrigin::Runtime: return "Injector Runtime";
			case ResourceOrigin::Disk:
			default: return "Offline / Disk";
		}
	}

	const char* ResourceLifetimeName(ResourceLifetime lifetime)
	{
		switch (lifetime)
		{
			case ResourceLifetime::Transient: return "Transient";
			case ResourceLifetime::Persistent: return "Persistent";
			case ResourceLifetime::History: return "History";
			case ResourceLifetime::Immutable:
			default: return "Immutable";
		}
	}

	bool IsValidDownscaleFactor(uint32_t downscaleFactor)
	{
		return downscaleFactor >= 1 && downscaleFactor <= 16 &&
			(downscaleFactor & (downscaleFactor - 1)) == 0;
	}
}
