#pragma once

#include <cstdint>

#include <d3d12.h>

namespace HookD3D12
{
	struct RetiredPSODiagnostics
	{
		size_t pendingCount = 0;
		uint64_t retiredTotal = 0;
		uint64_t releasedTotal = 0;
		//a reload loop that consistently grows retirements shows up as this never draining.
		ULONGLONG oldestPendingAgeMilliseconds = 0;
	};

	//only replacements are indexed; the game's own PSOs are never retired by the injector.
	void RecordReplacementBinding(ID3D12PipelineState* pipelineState, ID3D12GraphicsCommandList* commandList);

	void TrackRetiredPipelineState(ID3D12PipelineState*& pipelineState);

	//for a PSO the caller keeps alive itself: the tracker AddRefs here and releases once proved.
	void TrackRetiredPipelineStateReference(ID3D12PipelineState* pipelineState);

	//called for every ExecuteCommandLists; the release proof fences each queue seen here.
	void ObserveSubmissionQueue(ID3D12CommandQueue* commandQueue);

	void ProcessRetiredPSOReleases();

	RetiredPSODiagnostics GetRetiredPSODiagnostics();

	bool IsRetirePending(ID3D12PipelineState* pipelineState);

	//device teardown only: releases without fence proof.
	void ShutdownRetiredPSOTracker();
} //namespace HookD3D12
