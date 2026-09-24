#pragma once

#include "Hooks.h"

namespace Hooks
{
	//release temporary discovery objects on every exit from hook initialization.
	struct DummyObjectCleanupGuard
	{
		~DummyObjectCleanupGuard()
		{
			CleanupDummyObjects();
		}
	};
} //namespace Hooks
