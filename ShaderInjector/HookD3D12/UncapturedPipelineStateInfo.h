#pragma once

#include <cstdint>
#include <d3d12.h>
#include <memory>
#include <string>
#include <vector>
#include <wrl/client.h>

#include "HookD3D12/PipelineStateInfo.h"
#include "ShaderTarget/ShaderTarget.h"

namespace HookD3D12
{
	struct UncapturedPipelineStateInfo
	{
		ID3D12PipelineState* pipelineState = nullptr;
		// Keep the exact verified variant for render passes on warmed-cache runs.
		std::shared_ptr<const PipelineStateInfo> rebuildTemplate;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> rebuildRootSignature;
		uint64_t cachedBlobHash = 0;
		SIZE_T cachedBlobSize = 0;
		std::vector<uint8_t> cachedBlob;
		bool attemptedReplacement = false;
		bool retryReplacementOnRootSignatureChange = false;
		ID3D12PipelineState* replacementPipelineState = nullptr;
		ID3D12RootSignature* observedGraphicsRootSignature = nullptr;
		ID3D12RootSignature* observedComputeRootSignature = nullptr;
		std::string activeShaderTargetName;
		ShaderTarget::ShaderType activeShaderTargetType = ShaderTarget::Unknown;
		uint64_t activeShaderTargetHash = 0;
		uint8_t shaderTargetApplyFailureCount = 0;
		bool shaderTargetApplyRetryQueued = false;
	};
}
