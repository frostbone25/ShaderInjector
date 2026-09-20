#pragma once

#include <cstdint>

namespace RenderPassRuntime
{
	enum class RootBindingType : uint8_t
	{
		None,
		DescriptorTable,
		ConstantBufferView,
		ShaderResourceView,
		UnorderedAccessView,
		Constants
	};
}
