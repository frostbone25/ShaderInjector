#pragma once

#include <cstdint>

namespace PerformanceMetrics
{
	enum class Counter : uint8_t
	{
		Present,
		DrawInstanced,
		DrawIndexedInstanced,
		Dispatch,
		ExecuteIndirect,
		ExecutionBoundaryCandidate,
		ExecutionBoundaryRecorded,
		DescriptorCopy,
		DescriptorCopySimple,
		DescriptorCopyRegistryHit,
		MipPassAttempted,
		MipPassSucceeded,
		MipPassFailed,
		MipLevelDraw,
		MipTexels,
		MipResourceCreated,
		MipResourceReused,
		CustomPassAttempted,
		CustomPassSucceeded,
		ReplacementPassAttempted,
		ReplacementPassSucceeded,
		ShaderResourceBindAttempted,
		ShaderResourceBindSucceeded,
		Count,
	};

	enum class Timing : uint8_t
	{
		DescriptorCopyPropagation,
		DescriptorCopySimplePropagation,
		DescriptorRegistryTrackedPropagation,
		RegisterShaderResourceView,
		SetPipelineStateHook,
		ExecutionHookLookup,
		PresentShaderMaintenance,
		TrackPipelineState,
		TrackDescriptorHeaps,
		TrackRootDescriptorTable,
		TrackGraphicsState,
		RecordExecutionBoundary,
		BuildMipGraphicsState,
		PrepareMipChains,
		ResolveMipPass,
		EnsureMipResources,
		RecordMipGeneration,
		BindMipDescriptors,
		RestoreMipState,
		BindShaderResources,
		RestoreShaderResources,
		ExecuteCustomPass,
		ResolveReplacementPipeline,
		RetireMipSubmissions,
		Count,
	};

	void Increment(Counter counter, uint64_t amount = 1);

	// Hot paths may request sampled timing. Invocation counts remain exact, while
	// only every Nth call pays for two high-resolution clock reads.
	class ScopedTimer
	{
	public:
		explicit ScopedTimer(Timing timing, uint32_t sampleEvery = 1);
		~ScopedTimer();

		ScopedTimer(const ScopedTimer&) = delete;
		ScopedTimer& operator=(const ScopedTimer&) = delete;

	private:
		void* accumulator_ = nullptr;
		uint64_t startTicks_ = 0;
		bool sampled_ = false;
	};

	// Called once from the Present path. It records the frame and emits one
	// aggregate report after each five-second measurement window.
	bool RecordPresentAndMaybeLog();
}
