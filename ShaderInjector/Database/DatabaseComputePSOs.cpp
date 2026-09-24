//capture compute pipeline state objects and keep the data needed for later rebuilds.

#include <mutex>
#include <vector>

#include "HookD3D12/HookD3D12.h"
#include "Hash/Hash.h"
#include "ShaderAutomaticDiscovery.h"
#include "ShaderModelDetector.h"

namespace HookD3D12
{
	std::vector<ComputePipelineInfo> gComputePipelines;

	void CaptureComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* pipelineDescription, ID3D12PipelineState* pipelineState, bool shouldRegisterAsKnownPipeline)
	{
		if (!pipelineDescription || !pipelineState)
			return;

		ComputePipelineInfo capturedComputePipeline{};
		capturedComputePipeline.pipelineState = pipelineState;

		//copy the shader bytes because the D3D12 descriptor only borrows the caller's memory.
		//the copy stays valid after the game's CreateComputePipelineState call returns.
		if (pipelineDescription->CS.pShaderBytecode && pipelineDescription->CS.BytecodeLength)
		{
			ShaderModelDetector::ObserveShaderBytecode(ShaderTarget::ComputeShader, pipelineDescription->CS.pShaderBytecode, pipelineDescription->CS.BytecodeLength);
			capturedComputePipeline.computeShaderHash = Hash::HashMemory(pipelineDescription->CS.pShaderBytecode, pipelineDescription->CS.BytecodeLength);
			capturedComputePipeline.computeShaderBytecodeSize = pipelineDescription->CS.BytecodeLength;
			capturedComputePipeline.computeShaderBytecode.assign(static_cast<const uint8_t*>(pipelineDescription->CS.pShaderBytecode), static_cast<const uint8_t*>(pipelineDescription->CS.pShaderBytecode) + pipelineDescription->CS.BytecodeLength);
		}

		//copy the descriptor after the bytecode so its shader pointer can be redirected to our owned vector.
		capturedComputePipeline.originalDescription = *pipelineDescription;
		capturedComputePipeline.originalDescription.CS.pShaderBytecode = nullptr;
		capturedComputePipeline.originalDescription.CS.BytecodeLength = 0;

		if (!capturedComputePipeline.computeShaderBytecode.empty())
		{
			capturedComputePipeline.originalDescription.CS.pShaderBytecode = capturedComputePipeline.computeShaderBytecode.data();
			capturedComputePipeline.originalDescription.CS.BytecodeLength = capturedComputePipeline.computeShaderBytecode.size();
		}

		//the game's cached blob belongs to its original device/cache and must not be reused for rebuilds.
		capturedComputePipeline.originalDescription.CachedPSO = {};

		//keep the root signature alive because rebuild work may happen after the caller releases its reference.
		if (capturedComputePipeline.originalDescription.pRootSignature)
			capturedComputePipeline.originalDescription.pRootSignature->AddRef();

		std::lock_guard<std::mutex> pipelineLock(gPipelineMutex);

		if (shouldRegisterAsKnownPipeline)
			RegisterKnownPipelineStateLocked(capturedComputePipeline.pipelineState);

		gComputePipelines.push_back(capturedComputePipeline);
	}
} //namespace HookD3D12
