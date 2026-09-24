#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ShaderModelDetector
{
	constexpr size_t supportedShaderModelCount = 9;

	//each shader stage counts the models found in captured bytecode without allocating in a hook.
	struct ShaderModelStageObservations
	{
		std::array<std::atomic<uint32_t>, supportedShaderModelCount> modelCounts{};
	};
} //namespace ShaderModelDetector
