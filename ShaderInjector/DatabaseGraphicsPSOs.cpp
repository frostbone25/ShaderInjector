//DatabaseGraphicsPSOs.cpp
#include <mutex>
#include <vector>

//custom
#include "DatabaseGraphicsPSOs.h"
#include "Hash.h"
#include "HookD3D12PipelineRegistry.h"
#include "ShaderAutomaticDiscovery.h"

namespace HookD3D12
{
	std::vector<GraphicsPipelineInfo> gGraphicsPipelines;

	namespace
	{
		std::vector<ComputePipelineInfo> gComputePipelines;
	}

	void CaptureGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pipelineDescription, ID3D12PipelineState* pipelineState)
	{
		if (!pipelineDescription || !pipelineState)
			return;

		GraphicsPipelineInfo capturedPipeline{};
		capturedPipeline.pipelineState = pipelineState;

		// Copy every shader bytecode blob that exists on the original graphics PSO.
		// The D3D12 desc points into caller-owned memory, so replacement rebuilds must use our durable copies.
		if (pipelineDescription->VS.pShaderBytecode && pipelineDescription->VS.BytecodeLength)
		{
			capturedPipeline.vsHash = Hash::HashMemory(pipelineDescription->VS.pShaderBytecode, pipelineDescription->VS.BytecodeLength);
			capturedPipeline.vsSize = pipelineDescription->VS.BytecodeLength;
			capturedPipeline.vsBytecode.assign((const uint8_t*)pipelineDescription->VS.pShaderBytecode, (const uint8_t*)pipelineDescription->VS.pShaderBytecode + pipelineDescription->VS.BytecodeLength);
		}

		if (pipelineDescription->PS.pShaderBytecode && pipelineDescription->PS.BytecodeLength)
		{
			capturedPipeline.psHash = Hash::HashMemory(pipelineDescription->PS.pShaderBytecode, pipelineDescription->PS.BytecodeLength);
			capturedPipeline.psSize = pipelineDescription->PS.BytecodeLength;
			capturedPipeline.psBytecode.assign((const uint8_t*)pipelineDescription->PS.pShaderBytecode, (const uint8_t*)pipelineDescription->PS.pShaderBytecode + pipelineDescription->PS.BytecodeLength);
		}

		if (pipelineDescription->GS.pShaderBytecode && pipelineDescription->GS.BytecodeLength)
		{
			capturedPipeline.gsHash = Hash::HashMemory(pipelineDescription->GS.pShaderBytecode, pipelineDescription->GS.BytecodeLength);
			capturedPipeline.gsSize = pipelineDescription->GS.BytecodeLength;
			capturedPipeline.gsBytecode.assign((const uint8_t*)pipelineDescription->GS.pShaderBytecode, (const uint8_t*)pipelineDescription->GS.pShaderBytecode + pipelineDescription->GS.BytecodeLength);
		}

		if (pipelineDescription->HS.pShaderBytecode && pipelineDescription->HS.BytecodeLength)
		{
			capturedPipeline.hsHash = Hash::HashMemory(pipelineDescription->HS.pShaderBytecode, pipelineDescription->HS.BytecodeLength);
			capturedPipeline.hsSize = pipelineDescription->HS.BytecodeLength;
			capturedPipeline.hsBytecode.assign((const uint8_t*)pipelineDescription->HS.pShaderBytecode, (const uint8_t*)pipelineDescription->HS.pShaderBytecode + pipelineDescription->HS.BytecodeLength);
		}

		if (pipelineDescription->DS.pShaderBytecode && pipelineDescription->DS.BytecodeLength)
		{
			capturedPipeline.dsHash = Hash::HashMemory(pipelineDescription->DS.pShaderBytecode, pipelineDescription->DS.BytecodeLength);
			capturedPipeline.dsSize = pipelineDescription->DS.BytecodeLength;
			capturedPipeline.dsBytecode.assign((const uint8_t*)pipelineDescription->DS.pShaderBytecode, (const uint8_t*)pipelineDescription->DS.pShaderBytecode + pipelineDescription->DS.BytecodeLength);
		}

		capturedPipeline.originalDesc = *pipelineDescription;
		capturedPipeline.originalDesc.VS = { capturedPipeline.vsBytecode.empty() ? nullptr : capturedPipeline.vsBytecode.data(), capturedPipeline.vsBytecode.size() };
		capturedPipeline.originalDesc.PS = { capturedPipeline.psBytecode.empty() ? nullptr : capturedPipeline.psBytecode.data(), capturedPipeline.psBytecode.size() };
		capturedPipeline.originalDesc.GS = { capturedPipeline.gsBytecode.empty() ? nullptr : capturedPipeline.gsBytecode.data(), capturedPipeline.gsBytecode.size() };
		capturedPipeline.originalDesc.HS = { capturedPipeline.hsBytecode.empty() ? nullptr : capturedPipeline.hsBytecode.data(), capturedPipeline.hsBytecode.size() };
		capturedPipeline.originalDesc.DS = { capturedPipeline.dsBytecode.empty() ? nullptr : capturedPipeline.dsBytecode.data(), capturedPipeline.dsBytecode.size() };
		capturedPipeline.originalDesc.pRootSignature = pipelineDescription->pRootSignature;

		if (capturedPipeline.originalDesc.pRootSignature)
			capturedPipeline.originalDesc.pRootSignature->AddRef();

		// Repoint descriptor arrays to vectors owned by GraphicsPipelineInfo.
		if (pipelineDescription->InputLayout.pInputElementDescs && pipelineDescription->InputLayout.NumElements > 0)
		{
			capturedPipeline.inputElements.assign(pipelineDescription->InputLayout.pInputElementDescs, pipelineDescription->InputLayout.pInputElementDescs + pipelineDescription->InputLayout.NumElements);
			capturedPipeline.originalDesc.InputLayout.pInputElementDescs = capturedPipeline.inputElements.data();
			capturedPipeline.originalDesc.InputLayout.NumElements = (UINT)capturedPipeline.inputElements.size();
		}
		else
		{
			capturedPipeline.originalDesc.InputLayout.pInputElementDescs = nullptr;
			capturedPipeline.originalDesc.InputLayout.NumElements = 0;
		}

		if (pipelineDescription->StreamOutput.pSODeclaration && pipelineDescription->StreamOutput.NumEntries > 0)
		{
			capturedPipeline.soDeclarations.assign(pipelineDescription->StreamOutput.pSODeclaration, pipelineDescription->StreamOutput.pSODeclaration + pipelineDescription->StreamOutput.NumEntries);
			capturedPipeline.originalDesc.StreamOutput.pSODeclaration = capturedPipeline.soDeclarations.data();
		}
		else
		{
			capturedPipeline.originalDesc.StreamOutput.pSODeclaration = nullptr;
			capturedPipeline.originalDesc.StreamOutput.NumEntries = 0;
		}

		if (pipelineDescription->StreamOutput.pBufferStrides && pipelineDescription->StreamOutput.NumStrides > 0)
		{
			capturedPipeline.soStrides.assign(pipelineDescription->StreamOutput.pBufferStrides, pipelineDescription->StreamOutput.pBufferStrides + pipelineDescription->StreamOutput.NumStrides);
			capturedPipeline.originalDesc.StreamOutput.pBufferStrides = capturedPipeline.soStrides.data();
		}
		else
		{
			capturedPipeline.originalDesc.StreamOutput.pBufferStrides = nullptr;
			capturedPipeline.originalDesc.StreamOutput.NumStrides = 0;
		}

		// Cached PSO blobs are tied to the game's original cache/device and are invalid for our replacement rebuilds.
		capturedPipeline.originalDesc.CachedPSO.pCachedBlob = nullptr;
		capturedPipeline.originalDesc.CachedPSO.CachedBlobSizeInBytes = 0;

		{
			std::lock_guard<std::mutex> lock(gPipelineMutex);
			RegisterKnownPipelineStateLocked(pipelineState);
			gGraphicsPipelines.push_back(capturedPipeline);
			MarkShaderTargetApplyDirty();
		}

		ShaderAutomaticDiscovery::ProcessCapturedGraphicsPipeline(capturedPipeline);
	}

	void CaptureComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* pipelineDescription, ID3D12PipelineState* pipelineState, bool shouldRegisterAsKnownPipeline)
	{
		if (!pipelineDescription || !pipelineState)
			return;

		ComputePipelineInfo capturedPipeline{};
		capturedPipeline.pipelineState = pipelineState;

		if (pipelineDescription->CS.pShaderBytecode && pipelineDescription->CS.BytecodeLength)
		{
			capturedPipeline.csHash = Hash::HashMemory(pipelineDescription->CS.pShaderBytecode, pipelineDescription->CS.BytecodeLength);
			capturedPipeline.csSize = pipelineDescription->CS.BytecodeLength;
		}

		std::lock_guard<std::mutex> lock(gPipelineMutex);

		if (shouldRegisterAsKnownPipeline)
			RegisterKnownPipelineStateLocked(capturedPipeline.pipelineState);

		gComputePipelines.push_back(capturedPipeline);
	}
}
