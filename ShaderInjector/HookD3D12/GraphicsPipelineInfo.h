#pragma once

#include <cstdint>
#include <d3d12.h>
#include <string>
#include <vector>

#include "ShaderTarget/ShaderTarget.h"

namespace HookD3D12
{
	struct GraphicsPipelineInfo
	{
		ID3D12PipelineState* pipelineState = nullptr;

		uint64_t vertexShaderHash = 0;
		SIZE_T vertexShaderBytecodeSize = 0;

		uint64_t pixelShaderHash = 0;
		SIZE_T pixelShaderBytecodeSize = 0;

		uint64_t geometryShaderHash = 0;
		SIZE_T geometryShaderBytecodeSize = 0;

		uint64_t hullShaderHash = 0;
		SIZE_T hullShaderBytecodeSize = 0;

		uint64_t domainShaderHash = 0;
		SIZE_T domainShaderBytecodeSize = 0;

		std::vector<uint8_t> vertexShaderBytecode;
		std::vector<uint8_t> pixelShaderBytecode;
		std::vector<uint8_t> geometryShaderBytecode;
		std::vector<uint8_t> hullShaderBytecode;
		std::vector<uint8_t> domainShaderBytecode;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC originalDescription = {};

		bool pixelShaderDisabled = false;
		ID3D12PipelineState* pipelineStateWithoutPixelShader = nullptr;

		ID3D12PipelineState* pipelineStateWithReplacement = nullptr;
		std::string activeShaderTargetName;
		ShaderTarget::ShaderType activeShaderTargetType = ShaderTarget::Unknown;
		uint64_t activeShaderTargetHash = 0;
		bool activeShaderTargetUsesFallback = false;
		uint8_t shaderTargetApplyFailureCount = 0;
		bool shaderTargetApplyRetryQueued = false;

		std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
		std::vector<std::string> inputElementSemanticNames;
		std::vector<D3D12_SO_DECLARATION_ENTRY> streamOutputDeclarations;
		std::vector<std::string> streamOutputSemanticNames;
		std::vector<UINT> streamOutputStrides;
	};
}
