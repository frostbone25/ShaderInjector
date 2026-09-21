#pragma once

#include <cstdint>
#include <d3d12.h>
#include <string>
#include <vector>

#include "ShaderTarget/ShaderTarget.h"

namespace HookD3D12
{
	struct PipelineStateInfo
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
		uint64_t csHash = 0;
		SIZE_T csSize = 0;
		uint64_t asHash = 0;
		SIZE_T asSize = 0;
		uint64_t msHash = 0;
		SIZE_T msSize = 0;

		bool isGraphics = false;
		bool isCompute = false;
		ID3D12RootSignature* rootSignature = nullptr;

		std::vector<uint8_t> streamBlob;

		bool vsDisabled = false;
		ID3D12PipelineState* psoWithoutVS = nullptr;
		bool psDisabled = false;
		ID3D12PipelineState* psoWithoutPS = nullptr;
		bool csDisabled = false;
		ID3D12PipelineState* psoWithoutCS = nullptr;
		bool gsDisabled = false;
		ID3D12PipelineState* psoWithoutGS = nullptr;
		bool hsDisabled = false;
		ID3D12PipelineState* psoWithoutHS = nullptr;
		bool dsDisabled = false;
		ID3D12PipelineState* psoWithoutDS = nullptr;

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
		bool hasViewInstancing = false;
		D3D12_VIEW_INSTANCING_FLAGS viewInstancingFlags = D3D12_VIEW_INSTANCING_FLAG_NONE;
		std::vector<D3D12_VIEW_INSTANCE_LOCATION> viewInstanceLocations;

		std::vector<uint8_t> vsBytecode;
		std::vector<uint8_t> psBytecode;
		std::vector<uint8_t> gsBytecode;
		std::vector<uint8_t> hsBytecode;
		std::vector<uint8_t> dsBytecode;
		std::vector<uint8_t> csBytecode;
		std::vector<uint8_t> asBytecode;
		std::vector<uint8_t> msBytecode;
	};
}
