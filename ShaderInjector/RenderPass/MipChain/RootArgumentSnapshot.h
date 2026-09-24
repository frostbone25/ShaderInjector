#pragma once

#include <cstdint>
#include <vector>

#include <d3d12.h>

#include "Enum/RenderPassMipChainRootArgumentType.h"

namespace RenderPassMipChain
{
	//save one root argument so the game's pipeline state can be restored afterward.
	struct RootArgumentSnapshot
	{
		RootArgumentType type = RootArgumentType::DescriptorTable;
		UINT rootParameterIndex = UINT32_MAX;
		uint64_t value = 0;
		std::vector<uint32_t> constants;
	};
} //namespace RenderPassMipChain
