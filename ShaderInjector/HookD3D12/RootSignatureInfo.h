#pragma once

#include <cstdint>
#include <vector>

namespace HookD3D12
{
	struct RootSignatureInfo
	{
		uint64_t rootSignatureHash = 0;
		std::vector<uint8_t> rootSignatureBlob;
	};
}
