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

		unsigned int commandListType = UINT_MAX;

		if (commandList)
			commandListType = static_cast<unsigned int>(commandList->GetType());

		unsigned int renderPassesEnabled = 0;

		if (RenderPassRuntime::HasEnabledRenderPasses())
			renderPassesEnabled = 1;

		ShaderInjectorIO::WriteToLogFile(StringHelper::Format(
			"HookD3D12RenderPass->%s: command-list execution hook active commandList=%p type=%u renderPassesEnabled=%u",
			operation,
			commandList,
			commandListType,
			renderPassesEnabled));
	}
} //namespace HookD3D12
