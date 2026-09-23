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
		uint64_t computeShaderHash = 0;
		SIZE_T computeShaderBytecodeSize = 0;
		uint64_t amplificationShaderHash = 0;
		SIZE_T amplificationShaderBytecodeSize = 0;
		uint64_t meshShaderHash = 0;
		SIZE_T meshShaderBytecodeSize = 0;

		bool isGraphics = false;
		bool isCompute = false;
		ID3D12RootSignature* rootSignature = nullptr;

		std::vector<uint8_t> streamBlob;

		bool vertexShaderDisabled = false;
		ID3D12PipelineState* pipelineStateWithoutVertexShader = nullptr;
		bool pixelShaderDisabled = false;
		ID3D12PipelineState* pipelineStateWithoutPixelShader = nullptr;
		bool computeShaderDisabled = false;
		ID3D12PipelineState* pipelineStateWithoutComputeShader = nullptr;
		bool geometryShaderDisabled = false;
		ID3D12PipelineState* pipelineStateWithoutGeometryShader = nullptr;
		bool hullShaderDisabled = false;
		ID3D12PipelineState* pipelineStateWithoutHullShader = nullptr;
		bool domainShaderDisabled = false;
		ID3D12PipelineState* pipelineStateWithoutDomainShader = nullptr;

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
		bool hasViewInstancing = false;
		D3D12_VIEW_INSTANCING_FLAGS viewInstancingFlags = D3D12_VIEW_INSTANCING_FLAG_NONE;
		std::vector<D3D12_VIEW_INSTANCE_LOCATION> viewInstanceLocations;

		std::vector<uint8_t> vertexShaderBytecode;
		std::vector<uint8_t> pixelShaderBytecode;
		std::vector<uint8_t> geometryShaderBytecode;
		std::vector<uint8_t> hullShaderBytecode;
		std::vector<uint8_t> domainShaderBytecode;
		std::vector<uint8_t> computeShaderBytecode;
		std::vector<uint8_t> amplificationShaderBytecode;
		std::vector<uint8_t> meshShaderBytecode;
	};
}
