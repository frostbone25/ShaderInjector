#pragma once

#include <cstdint>
#include <atomic>
#include <d3d12.h>
#include <memory>
#include <string>
#include <vector>
#include <wrl/client.h>

#include "HookD3D12/PipelineStateInfo.h"
#include "HookD3D12/UncapturedRootSignatureAttempts.h"
#include "ShaderTarget/ShaderTarget.h"

namespace HookD3D12
{
	struct PendingPipelineTemplateLoad
	{
		std::shared_ptr<PipelineStateInfo> pipelineTemplate;
		std::atomic<bool> completed = false;
		std::atomic<bool> queued = false;
		bool succeeded = false;
	};

	struct UncapturedPipelineStateInfo
	{
		ID3D12PipelineState* pipelineState = nullptr;
		//Keep the exact verified variant for render passes on warmed-cache runs.
		std::shared_ptr<const PipelineStateInfo> rebuildTemplate;
		std::shared_ptr<PipelineStateInfo> pendingRebuildTemplate;
		std::shared_ptr<PendingPipelineTemplateLoad> pendingTemplateLoad;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> rebuildRootSignature;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> pendingRebuildRootSignature;
		ShaderTarget::ShaderTargetDisk pendingTemplateReplacement;
		std::string pendingTemplateName;
		std::string pendingReplacementName;
		uint64_t pendingReplacementHash = 0;
		uint64_t pendingReplacementBlobGeneration = 0;
		SIZE_T pendingTemplateMatchingBytes = 0;
		ShaderTarget::ShaderType pendingReplacementType = ShaderTarget::Unknown;
		bool pendingRootSignatureWasObserved = false;
		uint64_t cachedBlobHash = 0;
		SIZE_T cachedBlobSize = 0;
		std::vector<uint8_t> cachedBlob;
		bool attemptedReplacement = false;
		bool retryReplacementOnRootSignatureChange = false;
		UncapturedRootSignatureAttempts rootSignatureAttempts;
		ShaderTarget::ShaderType matchedShaderType = ShaderTarget::Unknown;
		ID3D12PipelineState* replacementPipelineState = nullptr;
		ID3D12RootSignature* observedGraphicsRootSignature = nullptr;
		ID3D12RootSignature* observedComputeRootSignature = nullptr;
		std::string activeShaderTargetName;
		ShaderTarget::ShaderType activeShaderTargetType = ShaderTarget::Unknown;
		uint64_t activeShaderTargetHash = 0;
		ID3D12PipelineState* pendingSwapPipelineState = nullptr;
		uint8_t shaderTargetApplyFailureCount = 0;
		bool shaderTargetApplyRetryQueued = false;
		bool pendingAsyncRebuild = false;
	};
} //namespace HookD3D12
