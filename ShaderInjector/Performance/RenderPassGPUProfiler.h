#pragma once

#include <string>

#include <d3d12.h>

#include "Performance/GPUTimingToken.h"
#include "Performance/GPUPerformanceSnapshot.h"

namespace RenderPassGPUProfiler
{
	//sample a whole execution chain occasionally; this never delays or skips a render pass.
	class ScopedExecutionSample
	{
		ID3D12GraphicsCommandList* previousCommandList = nullptr;

	public:
		ScopedExecutionSample(ID3D12GraphicsCommandList* commandList, const std::string& modifiedShaderID, bool afterBoundary = false);
		~ScopedExecutionSample();

		ScopedExecutionSample(const ScopedExecutionSample&) = delete;
		ScopedExecutionSample& operator=(const ScopedExecutionSample&) = delete;
	};

	class ScopedPassTiming
	{
		ID3D12GraphicsCommandList* commandList = nullptr;
		GPUTimingToken token;

	public:
		ScopedPassTiming(ID3D12GraphicsCommandList* commandList, const std::string& passName);
		~ScopedPassTiming();

		ScopedPassTiming(const ScopedPassTiming&) = delete;
		ScopedPassTiming& operator=(const ScopedPassTiming&) = delete;
	};

	//replacement setup is not a draw; time the actual original draw/dispatch separately.
	void BeginOriginalDraw(ID3D12GraphicsCommandList* commandList, const std::string& modifiedShaderID);
	void EndOriginalDraw(ID3D12GraphicsCommandList* commandList);

	bool HasRecordedCommandListWork();
	void ResetCommandList(ID3D12GraphicsCommandList* commandList);
	void NotifyCommandListsSubmitted(ID3D12CommandQueue* commandQueue, UINT commandListCount, ID3D12CommandList* const* commandLists);

	//read only completed submissions; an unfinished sample is left alone, never waited on.
	void LogPerformanceStatistics();
	PerformanceSnapshot GetSnapshot();
	void ReleaseResources();
}
