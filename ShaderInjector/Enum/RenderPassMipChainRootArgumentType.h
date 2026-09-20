#pragma once

#include <cstdint>

namespace RenderPassMipChain
{
	enum class RootArgumentType : uint8_t
	{
		DescriptorTable,
		ConstantBufferView,
		ShaderResourceView,
		UnorderedAccessView,
		Constants
	};
}
