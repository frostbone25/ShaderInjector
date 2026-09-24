//capture graphics pipeline state objects and preserve all data needed for later rebuilds.

#include <mutex>
#include <vector>

#include "HookD3D12/HookD3D12.h"
#include "Hash/Hash.h"
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
			capturedGraphicsPipeline.vertexShaderHash = Hash::HashMemory(pipelineDescription->VS.pShaderBytecode, pipelineDescription->VS.BytecodeLength);
			capturedGraphicsPipeline.vertexShaderBytecodeSize = pipelineDescription->VS.BytecodeLength;
			capturedGraphicsPipeline.vertexShaderBytecode.assign(static_cast<const uint8_t*>(pipelineDescription->VS.pShaderBytecode), static_cast<const uint8_t*>(pipelineDescription->VS.pShaderBytecode) + pipelineDescription->VS.BytecodeLength);
		}

		if (pipelineDescription->PS.pShaderBytecode && pipelineDescription->PS.BytecodeLength)
		{
			ShaderModelDetector::ObserveShaderBytecode(ShaderTarget::PixelShader, pipelineDescription->PS.pShaderBytecode, pipelineDescription->PS.BytecodeLength);
			capturedGraphicsPipeline.pixelShaderHash = Hash::HashMemory(pipelineDescription->PS.pShaderBytecode, pipelineDescription->PS.BytecodeLength);
			capturedGraphicsPipeline.pixelShaderBytecodeSize = pipelineDescription->PS.BytecodeLength;
			capturedGraphicsPipeline.pixelShaderBytecode.assign(static_cast<const uint8_t*>(pipelineDescription->PS.pShaderBytecode), static_cast<const uint8_t*>(pipelineDescription->PS.pShaderBytecode) + pipelineDescription->PS.BytecodeLength);
		}

		if (pipelineDescription->GS.pShaderBytecode && pipelineDescription->GS.BytecodeLength)
		{
			ShaderModelDetector::ObserveShaderBytecode(ShaderTarget::GeometryShader, pipelineDescription->GS.pShaderBytecode, pipelineDescription->GS.BytecodeLength);
			capturedGraphicsPipeline.geometryShaderHash = Hash::HashMemory(pipelineDescription->GS.pShaderBytecode, pipelineDescription->GS.BytecodeLength);
			capturedGraphicsPipeline.geometryShaderBytecodeSize = pipelineDescription->GS.BytecodeLength;
			capturedGraphicsPipeline.geometryShaderBytecode.assign(static_cast<const uint8_t*>(pipelineDescription->GS.pShaderBytecode), static_cast<const uint8_t*>(pipelineDescription->GS.pShaderBytecode) + pipelineDescription->GS.BytecodeLength);
		}

		if (pipelineDescription->HS.pShaderBytecode && pipelineDescription->HS.BytecodeLength)
		{
			ShaderModelDetector::ObserveShaderBytecode(ShaderTarget::HullShader, pipelineDescription->HS.pShaderBytecode, pipelineDescription->HS.BytecodeLength);
			capturedGraphicsPipeline.hullShaderHash = Hash::HashMemory(pipelineDescription->HS.pShaderBytecode, pipelineDescription->HS.BytecodeLength);
			capturedGraphicsPipeline.hullShaderBytecodeSize = pipelineDescription->HS.BytecodeLength;
			capturedGraphicsPipeline.hullShaderBytecode.assign(static_cast<const uint8_t*>(pipelineDescription->HS.pShaderBytecode), static_cast<const uint8_t*>(pipelineDescription->HS.pShaderBytecode) + pipelineDescription->HS.BytecodeLength);
		}

		if (pipelineDescription->DS.pShaderBytecode && pipelineDescription->DS.BytecodeLength)
		{
			ShaderModelDetector::ObserveShaderBytecode(ShaderTarget::DomainShader, pipelineDescription->DS.pShaderBytecode, pipelineDescription->DS.BytecodeLength);
			capturedGraphicsPipeline.domainShaderHash = Hash::HashMemory(pipelineDescription->DS.pShaderBytecode, pipelineDescription->DS.BytecodeLength);
			capturedGraphicsPipeline.domainShaderBytecodeSize = pipelineDescription->DS.BytecodeLength;
			capturedGraphicsPipeline.domainShaderBytecode.assign(static_cast<const uint8_t*>(pipelineDescription->DS.pShaderBytecode), static_cast<const uint8_t*>(pipelineDescription->DS.pShaderBytecode) + pipelineDescription->DS.BytecodeLength);
		}

		capturedGraphicsPipeline.originalDescription = *pipelineDescription;

		//repoint each descriptor to the corresponding bytecode vector owned by the captured pipeline.
		capturedGraphicsPipeline.originalDescription.VS.pShaderBytecode = nullptr;
		capturedGraphicsPipeline.originalDescription.VS.BytecodeLength = 0;
		capturedGraphicsPipeline.originalDescription.PS.pShaderBytecode = nullptr;
		capturedGraphicsPipeline.originalDescription.PS.BytecodeLength = 0;
		capturedGraphicsPipeline.originalDescription.GS.pShaderBytecode = nullptr;
		capturedGraphicsPipeline.originalDescription.GS.BytecodeLength = 0;
		capturedGraphicsPipeline.originalDescription.HS.pShaderBytecode = nullptr;
		capturedGraphicsPipeline.originalDescription.HS.BytecodeLength = 0;
		capturedGraphicsPipeline.originalDescription.DS.pShaderBytecode = nullptr;
		capturedGraphicsPipeline.originalDescription.DS.BytecodeLength = 0;

		if (!capturedGraphicsPipeline.vertexShaderBytecode.empty())
		{
			capturedGraphicsPipeline.originalDescription.VS.pShaderBytecode = capturedGraphicsPipeline.vertexShaderBytecode.data();
			capturedGraphicsPipeline.originalDescription.VS.BytecodeLength = capturedGraphicsPipeline.vertexShaderBytecode.size();
		}

		if (!capturedGraphicsPipeline.pixelShaderBytecode.empty())
		{
			capturedGraphicsPipeline.originalDescription.PS.pShaderBytecode = capturedGraphicsPipeline.pixelShaderBytecode.data();
			capturedGraphicsPipeline.originalDescription.PS.BytecodeLength = capturedGraphicsPipeline.pixelShaderBytecode.size();
		}

		if (!capturedGraphicsPipeline.geometryShaderBytecode.empty())
		{
			capturedGraphicsPipeline.originalDescription.GS.pShaderBytecode = capturedGraphicsPipeline.geometryShaderBytecode.data();
			capturedGraphicsPipeline.originalDescription.GS.BytecodeLength = capturedGraphicsPipeline.geometryShaderBytecode.size();
		}

		if (!capturedGraphicsPipeline.hullShaderBytecode.empty())
		{
			capturedGraphicsPipeline.originalDescription.HS.pShaderBytecode = capturedGraphicsPipeline.hullShaderBytecode.data();
			capturedGraphicsPipeline.originalDescription.HS.BytecodeLength = capturedGraphicsPipeline.hullShaderBytecode.size();
		}

		if (!capturedGraphicsPipeline.domainShaderBytecode.empty())
		{
			capturedGraphicsPipeline.originalDescription.DS.pShaderBytecode = capturedGraphicsPipeline.domainShaderBytecode.data();
			capturedGraphicsPipeline.originalDescription.DS.BytecodeLength = capturedGraphicsPipeline.domainShaderBytecode.size();
		}

		capturedGraphicsPipeline.originalDescription.pRootSignature = pipelineDescription->pRootSignature;

		//keep the root signature alive because rebuild work may happen after the caller releases its reference.
		if (capturedGraphicsPipeline.originalDescription.pRootSignature)
			capturedGraphicsPipeline.originalDescription.pRootSignature->AddRef();

		//copy the input-layout descriptor array because the D3D12 descriptor only borrows the caller's array.
		if (pipelineDescription->InputLayout.pInputElementDescs && pipelineDescription->InputLayout.NumElements > 0)
		{
			capturedGraphicsPipeline.inputElements.assign(pipelineDescription->InputLayout.pInputElementDescs, pipelineDescription->InputLayout.pInputElementDescs + pipelineDescription->InputLayout.NumElements);
			capturedGraphicsPipeline.originalDescription.InputLayout.pInputElementDescs = capturedGraphicsPipeline.inputElements.data();
			capturedGraphicsPipeline.originalDescription.InputLayout.NumElements = static_cast<UINT>(capturedGraphicsPipeline.inputElements.size());
		}
		else
		{
			capturedGraphicsPipeline.originalDescription.InputLayout.pInputElementDescs = nullptr;
			capturedGraphicsPipeline.originalDescription.InputLayout.NumElements = 0;
		}

		//stream-output declarations and strides also borrow caller-owned arrays, so preserve both arrays in the capture.
		if (pipelineDescription->StreamOutput.pSODeclaration && pipelineDescription->StreamOutput.NumEntries > 0)
		{
			capturedGraphicsPipeline.streamOutputDeclarations.assign(pipelineDescription->StreamOutput.pSODeclaration, pipelineDescription->StreamOutput.pSODeclaration + pipelineDescription->StreamOutput.NumEntries);
			capturedGraphicsPipeline.originalDescription.StreamOutput.pSODeclaration = capturedGraphicsPipeline.streamOutputDeclarations.data();
		}
		else
		{
			capturedGraphicsPipeline.originalDescription.StreamOutput.pSODeclaration = nullptr;
			capturedGraphicsPipeline.originalDescription.StreamOutput.NumEntries = 0;
		}

		if (pipelineDescription->StreamOutput.pBufferStrides && pipelineDescription->StreamOutput.NumStrides > 0)
		{
			capturedGraphicsPipeline.streamOutputStrides.assign(pipelineDescription->StreamOutput.pBufferStrides, pipelineDescription->StreamOutput.pBufferStrides + pipelineDescription->StreamOutput.NumStrides);
			capturedGraphicsPipeline.originalDescription.StreamOutput.pBufferStrides = capturedGraphicsPipeline.streamOutputStrides.data();
		}
		else
		{
			capturedGraphicsPipeline.originalDescription.StreamOutput.pBufferStrides = nullptr;
			capturedGraphicsPipeline.originalDescription.StreamOutput.NumStrides = 0;
		}

		//the game's cached blob belongs to its original device/cache and must not be reused for replacement rebuilds.
		capturedGraphicsPipeline.originalDescription.CachedPSO.pCachedBlob = nullptr;
		capturedGraphicsPipeline.originalDescription.CachedPSO.CachedBlobSizeInBytes = 0;

		{
			std::lock_guard<std::mutex> pipelineLock(gPipelineMutex);
			RegisterKnownPipelineStateLocked(pipelineState);
			gGraphicsPipelines.push_back(capturedGraphicsPipeline);
			QueueShaderTargetApplyWork();
		}

		//run discovery after the durable capture has been registered.
		ShaderAutomaticDiscovery::ProcessCapturedGraphicsPipeline(capturedGraphicsPipeline);
	}
} //namespace HookD3D12
