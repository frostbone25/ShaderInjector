#pragma once

#include <cstdint>

namespace PerformanceMetrics
{
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
}
