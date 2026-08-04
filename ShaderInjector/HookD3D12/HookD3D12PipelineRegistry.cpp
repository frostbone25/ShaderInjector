//HookD3D12PipelineRegistry.cpp
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
	}

	void RegisterKnownPipelineStateLocked(ID3D12PipelineState* pipelineStateObject)
	{
		if (pipelineStateObject)
		{
			std::unique_lock<std::shared_mutex> lock(gPipelineStateRegistryMutex);
			gKnownPipelineStates.insert(pipelineStateObject);
		}
	}

	void UnregisterKnownPipelineStateLocked(ID3D12PipelineState* pipelineStateObject)
	{
		if (pipelineStateObject)
		{
			std::unique_lock<std::shared_mutex> lock(gPipelineStateRegistryMutex);
			gKnownPipelineStates.erase(pipelineStateObject);
		}
	}

	bool IsKnownPipelineStateLocked(ID3D12PipelineState* pipelineStateObject)
	{
		std::shared_lock<std::shared_mutex> lock(gPipelineStateRegistryMutex);
		return !pipelineStateObject || gKnownPipelineStates.find(pipelineStateObject) != gKnownPipelineStates.end();
	}

	bool MarkUntrackedBoundPipelineStateLocked(ID3D12PipelineState* pipelineStateObject)
	{
		std::unique_lock<std::shared_mutex> lock(gPipelineStateRegistryMutex);
		return pipelineStateObject && gUntrackedBoundPipelineStates.insert(pipelineStateObject).second;
	}
}
