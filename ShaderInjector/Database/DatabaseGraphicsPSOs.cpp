// capture graphics pipeline state objects and preserve all data needed for later rebuilds.

#include <mutex>
#include <vector>

#include "HookD3D12/HookD3D12.h"
#include "Hash.h"
#include "HookD3D12PipelineRegistry.h"
#include "ShaderAutomaticDiscovery.h"
#include "ShaderModelDetector.h"

namespace HookD3D12
{
	std::vector<GraphicsPipelineInfo> gGraphicsPipelines;

	void CaptureGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pipelineDescription, ID3D12PipelineState* pipelineState)
	{
		if (!pipelineDescription || !pipelineState)
			return;

		GraphicsPipelineInfo capturedGraphicsPipeline{};
		capturedGraphicsPipeline.pipelineState = pipelineState;

		//copy every shader blob because the D3D12 descriptor only borrows the caller's memory.
		//these vectors remain valid after the game's CreateGraphicsPipelineState call returns.
		if (pipelineDescription->VS.pShaderBytecode && pipelineDescription->VS.BytecodeLength)
		{
			ShaderModelDetector::ObserveShaderBytecode(ShaderTarget::VertexShader, pipelineDescription->VS.pShaderBytecode, pipelineDescription->VS.BytecodeLength);
			capturedGraphicsPipeline.vsHash = Hash::HashMemory(pipelineDescription->VS.pShaderBytecode, pipelineDescription->VS.BytecodeLength);
			capturedGraphicsPipeline.vsSize = pipelineDescription->VS.BytecodeLength;
			capturedGraphicsPipeline.vsBytecode.assign(static_cast<const uint8_t*>(pipelineDescription->VS.pShaderBytecode), static_cast<const uint8_t*>(pipelineDescription->VS.pShaderBytecode) + pipelineDescription->VS.BytecodeLength);
		}

		if (pipelineDescription->PS.pShaderBytecode && pipelineDescription->PS.BytecodeLength)
		{
			ShaderModelDetector::ObserveShaderBytecode(ShaderTarget::PixelShader, pipelineDescription->PS.pShaderBytecode, pipelineDescription->PS.BytecodeLength);
			capturedGraphicsPipeline.psHash = Hash::HashMemory(pipelineDescription->PS.pShaderBytecode, pipelineDescription->PS.BytecodeLength);
			capturedGraphicsPipeline.psSize = pipelineDescription->PS.BytecodeLength;
			capturedGraphicsPipeline.psBytecode.assign(static_cast<const uint8_t*>(pipelineDescription->PS.pShaderBytecode), static_cast<const uint8_t*>(pipelineDescription->PS.pShaderBytecode) + pipelineDescription->PS.BytecodeLength);
		}

		if (pipelineDescription->GS.pShaderBytecode && pipelineDescription->GS.BytecodeLength)
		{
			ShaderModelDetector::ObserveShaderBytecode(ShaderTarget::GeometryShader, pipelineDescription->GS.pShaderBytecode, pipelineDescription->GS.BytecodeLength);
			capturedGraphicsPipeline.gsHash = Hash::HashMemory(pipelineDescription->GS.pShaderBytecode, pipelineDescription->GS.BytecodeLength);
			capturedGraphicsPipeline.gsSize = pipelineDescription->GS.BytecodeLength;
			capturedGraphicsPipeline.gsBytecode.assign(static_cast<const uint8_t*>(pipelineDescription->GS.pShaderBytecode), static_cast<const uint8_t*>(pipelineDescription->GS.pShaderBytecode) + pipelineDescription->GS.BytecodeLength);
		}

		if (pipelineDescription->HS.pShaderBytecode && pipelineDescription->HS.BytecodeLength)
		{
			ShaderModelDetector::ObserveShaderBytecode(ShaderTarget::HullShader, pipelineDescription->HS.pShaderBytecode, pipelineDescription->HS.BytecodeLength);
			capturedGraphicsPipeline.hsHash = Hash::HashMemory(pipelineDescription->HS.pShaderBytecode, pipelineDescription->HS.BytecodeLength);
			capturedGraphicsPipeline.hsSize = pipelineDescription->HS.BytecodeLength;
			capturedGraphicsPipeline.hsBytecode.assign(static_cast<const uint8_t*>(pipelineDescription->HS.pShaderBytecode), static_cast<const uint8_t*>(pipelineDescription->HS.pShaderBytecode) + pipelineDescription->HS.BytecodeLength);
		}

		if (pipelineDescription->DS.pShaderBytecode && pipelineDescription->DS.BytecodeLength)
		{
			ShaderModelDetector::ObserveShaderBytecode(ShaderTarget::DomainShader, pipelineDescription->DS.pShaderBytecode, pipelineDescription->DS.BytecodeLength);
			capturedGraphicsPipeline.dsHash = Hash::HashMemory(pipelineDescription->DS.pShaderBytecode, pipelineDescription->DS.BytecodeLength);
			capturedGraphicsPipeline.dsSize = pipelineDescription->DS.BytecodeLength;
			capturedGraphicsPipeline.dsBytecode.assign(static_cast<const uint8_t*>(pipelineDescription->DS.pShaderBytecode), static_cast<const uint8_t*>(pipelineDescription->DS.pShaderBytecode) + pipelineDescription->DS.BytecodeLength);
		}

		capturedGraphicsPipeline.originalDesc = *pipelineDescription;

		//repoint each descriptor to the corresponding bytecode vector owned by the captured pipeline.
		capturedGraphicsPipeline.originalDesc.VS.pShaderBytecode = nullptr;
		capturedGraphicsPipeline.originalDesc.VS.BytecodeLength = 0;
		capturedGraphicsPipeline.originalDesc.PS.pShaderBytecode = nullptr;
		capturedGraphicsPipeline.originalDesc.PS.BytecodeLength = 0;
		capturedGraphicsPipeline.originalDesc.GS.pShaderBytecode = nullptr;
		capturedGraphicsPipeline.originalDesc.GS.BytecodeLength = 0;
		capturedGraphicsPipeline.originalDesc.HS.pShaderBytecode = nullptr;
		capturedGraphicsPipeline.originalDesc.HS.BytecodeLength = 0;
		capturedGraphicsPipeline.originalDesc.DS.pShaderBytecode = nullptr;
		capturedGraphicsPipeline.originalDesc.DS.BytecodeLength = 0;

		if (!capturedGraphicsPipeline.vsBytecode.empty())
		{
			capturedGraphicsPipeline.originalDesc.VS.pShaderBytecode = capturedGraphicsPipeline.vsBytecode.data();
			capturedGraphicsPipeline.originalDesc.VS.BytecodeLength = capturedGraphicsPipeline.vsBytecode.size();
		}

		if (!capturedGraphicsPipeline.psBytecode.empty())
		{
			capturedGraphicsPipeline.originalDesc.PS.pShaderBytecode = capturedGraphicsPipeline.psBytecode.data();
			capturedGraphicsPipeline.originalDesc.PS.BytecodeLength = capturedGraphicsPipeline.psBytecode.size();
		}

		if (!capturedGraphicsPipeline.gsBytecode.empty())
		{
			capturedGraphicsPipeline.originalDesc.GS.pShaderBytecode = capturedGraphicsPipeline.gsBytecode.data();
			capturedGraphicsPipeline.originalDesc.GS.BytecodeLength = capturedGraphicsPipeline.gsBytecode.size();
		}

		if (!capturedGraphicsPipeline.hsBytecode.empty())
		{
			capturedGraphicsPipeline.originalDesc.HS.pShaderBytecode = capturedGraphicsPipeline.hsBytecode.data();
			capturedGraphicsPipeline.originalDesc.HS.BytecodeLength = capturedGraphicsPipeline.hsBytecode.size();
		}

		if (!capturedGraphicsPipeline.dsBytecode.empty())
		{
			capturedGraphicsPipeline.originalDesc.DS.pShaderBytecode = capturedGraphicsPipeline.dsBytecode.data();
			capturedGraphicsPipeline.originalDesc.DS.BytecodeLength = capturedGraphicsPipeline.dsBytecode.size();
		}

		capturedGraphicsPipeline.originalDesc.pRootSignature = pipelineDescription->pRootSignature;

		//keep the root signature alive because rebuild work may happen after the caller releases its reference.
		if (capturedGraphicsPipeline.originalDesc.pRootSignature)
			capturedGraphicsPipeline.originalDesc.pRootSignature->AddRef();

		//copy the input-layout descriptor array because the D3D12 descriptor only borrows the caller's array.
		if (pipelineDescription->InputLayout.pInputElementDescs && pipelineDescription->InputLayout.NumElements > 0)
		{
			capturedGraphicsPipeline.inputElements.assign(pipelineDescription->InputLayout.pInputElementDescs, pipelineDescription->InputLayout.pInputElementDescs + pipelineDescription->InputLayout.NumElements);
			capturedGraphicsPipeline.originalDesc.InputLayout.pInputElementDescs = capturedGraphicsPipeline.inputElements.data();
			capturedGraphicsPipeline.originalDesc.InputLayout.NumElements = static_cast<UINT>(capturedGraphicsPipeline.inputElements.size());
		}
		else
		{
			capturedGraphicsPipeline.originalDesc.InputLayout.pInputElementDescs = nullptr;
			capturedGraphicsPipeline.originalDesc.InputLayout.NumElements = 0;
		}

		//stream-output declarations and strides also borrow caller-owned arrays, so preserve both arrays in the capture.
		if (pipelineDescription->StreamOutput.pSODeclaration && pipelineDescription->StreamOutput.NumEntries > 0)
		{
			capturedGraphicsPipeline.soDeclarations.assign(pipelineDescription->StreamOutput.pSODeclaration, pipelineDescription->StreamOutput.pSODeclaration + pipelineDescription->StreamOutput.NumEntries);
			capturedGraphicsPipeline.originalDesc.StreamOutput.pSODeclaration = capturedGraphicsPipeline.soDeclarations.data();
		}
		else
		{
			capturedGraphicsPipeline.originalDesc.StreamOutput.pSODeclaration = nullptr;
			capturedGraphicsPipeline.originalDesc.StreamOutput.NumEntries = 0;
		}

		if (pipelineDescription->StreamOutput.pBufferStrides && pipelineDescription->StreamOutput.NumStrides > 0)
		{
			capturedGraphicsPipeline.soStrides.assign(pipelineDescription->StreamOutput.pBufferStrides, pipelineDescription->StreamOutput.pBufferStrides + pipelineDescription->StreamOutput.NumStrides);
			capturedGraphicsPipeline.originalDesc.StreamOutput.pBufferStrides = capturedGraphicsPipeline.soStrides.data();
		}
		else
		{
			capturedGraphicsPipeline.originalDesc.StreamOutput.pBufferStrides = nullptr;
			capturedGraphicsPipeline.originalDesc.StreamOutput.NumStrides = 0;
		}

		//the game's cached blob belongs to its original device/cache and must not be reused for replacement rebuilds.
		capturedGraphicsPipeline.originalDesc.CachedPSO.pCachedBlob = nullptr;
		capturedGraphicsPipeline.originalDesc.CachedPSO.CachedBlobSizeInBytes = 0;

		{
			std::lock_guard<std::mutex> pipelineLock(gPipelineMutex);
			RegisterKnownPipelineStateLocked(pipelineState);
			gGraphicsPipelines.push_back(capturedGraphicsPipeline);
			QueueShaderTargetApplyWork();
		}

		//run discovery after the durable capture has been registered.
		ShaderAutomaticDiscovery::ProcessCapturedGraphicsPipeline(capturedGraphicsPipeline);
	}
}
