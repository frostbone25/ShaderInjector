#pragma once

#include <atomic>

#include "IO/ShaderInjectorIO.h"
#include "RenderPass/RenderPassRuntime.h"
#include "StringHelper.h"

namespace HookD3D12
{
	inline void LogFirstCommandHookHit(
		std::atomic<bool>& logged,
		const char* operation,
		ID3D12GraphicsCommandList* commandList)
	{
		if (logged.load(std::memory_order_relaxed))
			return;

		bool expected = false;
		if (!logged.compare_exchange_strong(expected, true, std::memory_order_relaxed, std::memory_order_relaxed))
			return;

		ShaderInjectorIO::WriteToLogFile(StringHelper::Format(
			"HookD3D12RenderPass->%s: command-list execution hook active commandList=%p type=%u renderPassesEnabled=%u",
			operation,
			commandList,
			commandList ? static_cast<unsigned int>(commandList->GetType()) : UINT_MAX,
			RenderPassRuntime::HasEnabledRenderPasses() ? 1u : 0u));
	}
}
