// Database PSO capture implementation.
#include <mutex>
#include <vector>

#include "HookD3D12/HookD3D12.h"
#include "Hash.h"
#include "HookD3D12PipelineRegistry.h"
#include "ShaderAutomaticDiscovery.h"
#include "ShaderModelDetector.h"

namespace HookD3D12
{
	std::vector<ComputePipelineInfo> gComputePipelines;


	void CaptureComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* pipelineDescription, ID3D12PipelineState* pipelineState, bool shouldRegisterAsKnownPipeline)
	{
		if (!pipelineDescription || !pipelineState)
			return;

		ComputePipelineInfo capturedPipeline{};
		capturedPipeline.pipelineState = pipelineState;

		if (pipelineDescription->CS.pShaderBytecode && pipelineDescription->CS.BytecodeLength)
		{
			ShaderModelDetector::ObserveShaderBytecode(ShaderTarget::ComputeShader, pipelineDescription->CS.pShaderBytecode, pipelineDescription->CS.BytecodeLength);
			capturedPipeline.csHash = Hash::HashMemory(pipelineDescription->CS.pShaderBytecode, pipelineDescription->CS.BytecodeLength);
			capturedPipeline.csSize = pipelineDescription->CS.BytecodeLength;
			capturedPipeline.csBytecode.assign(
				static_cast<const uint8_t*>(pipelineDescription->CS.pShaderBytecode),
				static_cast<const uint8_t*>(pipelineDescription->CS.pShaderBytecode) + pipelineDescription->CS.BytecodeLength);
		}
		capturedPipeline.originalDesc = *pipelineDescription;
		capturedPipeline.originalDesc.CS = {
			capturedPipeline.csBytecode.empty() ? nullptr : capturedPipeline.csBytecode.data(),
			capturedPipeline.csBytecode.size() };
		capturedPipeline.originalDesc.CachedPSO = {};
		if (capturedPipeline.originalDesc.pRootSignature)
			capturedPipeline.originalDesc.pRootSignature->AddRef();

		std::lock_guard<std::mutex> lock(gPipelineMutex);

		if (shouldRegisterAsKnownPipeline)
			RegisterKnownPipelineStateLocked(capturedPipeline.pipelineState);

		gComputePipelines.push_back(capturedPipeline);
	}
}
