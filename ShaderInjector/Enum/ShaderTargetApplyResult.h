#pragma once

#include <cstdint>

namespace HookD3D12
{
	enum class ShaderTargetApplyResult : uint8_t
	{
		NoMatch,
		Applied,
		RetryableFailure
	};
} //namespace HookD3D12
