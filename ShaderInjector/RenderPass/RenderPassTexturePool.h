#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "RenderPass/RenderPass.h"
#include "RenderPass/ReferenceExtent.h"
#include "RenderPass/ResolvedTextureDescription.h"
#include "RenderPass/ScopedInputTextureOverrides.h"
#include "RenderPass/TextureView.h"

namespace RenderPassTexturePool
{
	void OverrideInputTexture(
		const std::string& resourceId,
		ShaderResource::TemporalView temporalView,
		const TextureView& texture);
	bool GetInputTexture(
		const std::string& resourceId,
		ShaderResource::TemporalView temporalView,
		TextureView& outTexture);
	//A previous-frame input has no producer on the first frame. A typed null SRV
	//reads zero without inventing a current-frame dependency or a GPU allocation.
	bool GetHistoryBootstrapTexture(
		ID3D12Device* device,
		const std::string& resourceId,
		TextureView& outTexture);
	void MarkPassOutputsWritten(const RenderPass::RenderPassDisk& renderPass);

	void PublishConfigurations(const std::vector<RenderPass::RenderPassDisk>& renderPasses);
	bool EnsurePassResources(
		const RenderPass::RenderPassDisk& renderPass,
		ID3D12GraphicsCommandList* commandList,
		const ReferenceExtent& referenceExtent,
		std::string& outError);
	bool GetTexture(
		const std::string& resourceId,
		ShaderResource::TemporalView temporalView,
		TextureView& outTexture);
	bool EnsureUpsampleChainStages(
		const RenderPass::RenderPassDisk& renderPass,
		ID3D12GraphicsCommandList* commandList,
		const TextureView& sourceTexture,
		const TextureView& destinationTexture,
		bool computePipeline,
		std::vector<TextureView>& outStageTargets,
		std::string& outError);
	void AdvanceFrame();
	bool HasRecordedCommandListWork();
	void ResetCommandListRecording(ID3D12GraphicsCommandList* commandList);
	void NotifyCommandListsSubmitted(ID3D12CommandQueue* commandQueue, UINT commandListCount, ID3D12CommandList* const* commandLists);
	void LogPerformanceStatistics();
	void ReleaseResources();
} //namespace RenderPassTexturePool
