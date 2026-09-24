#pragma once

#include <string>

namespace DatabaseRenderPasses
{
	//reserve both a disk-safe ID and the label shown in the pass editor.
	struct AvailableRenderPassIdentity
	{
		std::string renderPassID;
		std::string displayName;
	};
} //namespace DatabaseRenderPasses
