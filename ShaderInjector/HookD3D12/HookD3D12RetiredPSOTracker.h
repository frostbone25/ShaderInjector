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
		ULONGLONG oldestPendingAgeMilliseconds = 0;
	};

	void RecordReplacementBinding(ID3D12PipelineState* pipelineState, ID3D12GraphicsCommandList* commandList);

	void TrackRetiredPipelineState(ID3D12PipelineState*& pipelineState);

	void TrackRetiredPipelineStateReference(ID3D12PipelineState* pipelineState);

	void ObserveSubmissionQueue(ID3D12CommandQueue* commandQueue);

	void ProcessRetiredPSOReleases();

	RetiredPSODiagnostics GetRetiredPSODiagnostics();

	bool IsRetirePending(ID3D12PipelineState* pipelineState);

	void ShutdownRetiredPSOTracker();
} //namespace HookD3D12
