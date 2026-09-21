#pragma once

#include <cstdint>
#include <vector>

namespace HookD3D12
{
	struct RootSignatureInfo
	{
		uint64_t hash = 0;
		std::vector<uint8_t> blob;
	};
}
