#pragma once

#include <memory>
#include <string>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "HookD3D12/GraphicsPipelineInfo.h"
#include "HookD3D12/PipelineStateInfo.h"
#include "ModifiedShader/ModifiedShaderPackageDisk.h"
#include "ShaderAnalysis/ShaderAnalysis.h"
#include "ShaderDiscovery/QueuedShader.h"

namespace ShaderAutomaticDiscovery
{
	//own every byte and pointer backing the worker's immutable preparation snapshot.
	struct TargetCreationJob
	{
		QueuedShader queuedShader;
		std::string modifiedShaderID;
		std::shared_ptr<const std::vector<ModifiedShader::ModifiedShaderPackageDisk>> modifiedShaders;
		size_t modifiedShaderIndex = 0;
		HookD3D12::GraphicsPipelineInfo graphicsPipeline{};
		HookD3D12::PipelineStateInfo streamPipeline{};
		Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignatureLifetime;
		bool isGraphicsPipeline = false;
		ShaderAnalysis::ShaderAnalysisDisk shaderAnalysis{};
		bool hasShaderAnalysis = false;
	};
} //namespace ShaderAutomaticDiscovery
