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

		uint64_t vsHash = 0;
		SIZE_T vsSize = 0;

		uint64_t psHash = 0;
		SIZE_T psSize = 0;

		uint64_t gsHash = 0;
		SIZE_T gsSize = 0;

		uint64_t hsHash = 0;
		SIZE_T hsSize = 0;

		uint64_t dsHash = 0;
		SIZE_T dsSize = 0;

		std::vector<uint8_t> vsBytecode;
		std::vector<uint8_t> psBytecode;
		std::vector<uint8_t> gsBytecode;
		std::vector<uint8_t> hsBytecode;
		std::vector<uint8_t> dsBytecode;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC originalDesc = {};

		bool psDisabled = false;
		ID3D12PipelineState* psoWithoutPS = nullptr;

		ID3D12PipelineState* psoWithReplacement = nullptr;
		std::string activeShaderTargetName;
		ShaderTarget::ShaderType activeShaderTargetType = ShaderTarget::Unknown;
		uint64_t activeShaderTargetHash = 0;
		bool activeShaderTargetUsesFallback = false;
		uint8_t shaderTargetApplyFailureCount = 0;
		bool shaderTargetApplyRetryQueued = false;

		std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
		std::vector<std::string> inputElementSemanticNames;
		std::vector<D3D12_SO_DECLARATION_ENTRY> soDeclarations;
		std::vector<std::string> soSemanticNames;
		std::vector<UINT> soStrides;
	};
}
