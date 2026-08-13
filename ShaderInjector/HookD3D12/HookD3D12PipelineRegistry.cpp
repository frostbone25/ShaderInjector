//HookD3D12PipelineRegistry.cpp
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>

//custom
#include "HookD3D12PipelineRegistry.h"

namespace HookD3D12
{
	namespace
	{
		std::unordered_set<ID3D12PipelineState*> gKnownPipelineStates;
		std::unordered_set<ID3D12PipelineState*> gUntrackedBoundPipelineStates;
		std::shared_mutex gPipelineStateRegistryMutex;
		std::atomic<uint64_t> gPipelineStateRegistryGeneration = 1;

		struct KnownPipelineCacheEntry
		{
			ID3D12PipelineState* pipelineState = nullptr;
			uint64_t generation = 0;
			bool known = false;
		};

		thread_local std::array<KnownPipelineCacheEntry, 256> gKnownPipelineCache;
	}

	void RegisterKnownPipelineStateLocked(ID3D12PipelineState* pipelineStateObject)
	{
		if (pipelineStateObject)
		{
			std::unique_lock<std::shared_mutex> lock(gPipelineStateRegistryMutex);
			if (gKnownPipelineStates.insert(pipelineStateObject).second)
				gPipelineStateRegistryGeneration.fetch_add(1, std::memory_order_release);
		}
	}

	void UnregisterKnownPipelineStateLocked(ID3D12PipelineState* pipelineStateObject)
	{
		if (pipelineStateObject)
		{
			std::unique_lock<std::shared_mutex> lock(gPipelineStateRegistryMutex);
			if (gKnownPipelineStates.erase(pipelineStateObject) != 0)
				gPipelineStateRegistryGeneration.fetch_add(1, std::memory_order_release);
		}
	}

	bool IsKnownPipelineStateLocked(ID3D12PipelineState* pipelineStateObject)
	{
		if (!pipelineStateObject)
			return true;

		const uint64_t generation = gPipelineStateRegistryGeneration.load(std::memory_order_acquire);
		KnownPipelineCacheEntry& cacheEntry = gKnownPipelineCache[
			(reinterpret_cast<uintptr_t>(pipelineStateObject) >> 4) % gKnownPipelineCache.size()];
		if (cacheEntry.pipelineState == pipelineStateObject && cacheEntry.generation == generation)
			return cacheEntry.known;

		std::shared_lock<std::shared_mutex> lock(gPipelineStateRegistryMutex);
		const bool known = gKnownPipelineStates.find(pipelineStateObject) != gKnownPipelineStates.end();
		cacheEntry = { pipelineStateObject, generation, known };
		return known;
	}

	bool MarkUntrackedBoundPipelineStateLocked(ID3D12PipelineState* pipelineStateObject)
	{
		std::unique_lock<std::shared_mutex> lock(gPipelineStateRegistryMutex);
		return pipelineStateObject && gUntrackedBoundPipelineStates.insert(pipelineStateObject).second;
	}
}
