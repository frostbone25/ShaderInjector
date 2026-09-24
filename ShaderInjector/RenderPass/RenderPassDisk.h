#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <d3d12.h>
#include "JsonHelper.h"
#include "ShaderResource/ShaderResource.h"
#include "Enum/RenderPassRenderPassType.h"
#include "Enum/RenderPassExecutionMode.h"
#include "Enum/RenderPassPassOperation.h"
#include "Enum/RenderPassDispatchMode.h"
#include "Enum/RenderPassViewportMode.h"
#include "Enum/RenderPassResourceAccess.h"
#include "Enum/RenderPassGameResourceViewType.h"
#include "Enum/RenderPassEventType.h"
#include "RenderPass/RenderPassSchema.h"
#include "RenderPass/DispatchPolicyDisk.h"
#include "RenderPass/ViewportPolicyDisk.h"
#include "RenderPass/LogicalResourceBindingDisk.h"
#include "RenderPass/RuntimeResourceDefinitionDisk.h"
#include "RenderPass/ShaderResourceReferenceDisk.h"
#include "RenderPass/SamplerStateDisk.h"
#include "RenderPass/InheritedGameBindingsDisk.h"
#include "RenderPass/EventReferenceDisk.h"

namespace RenderPass
{
	//store the saved pass settings alongside the paths and blobs resolved when it loads.
	struct RenderPassDisk
	{
		std::string format = formatName;
		int schemaVersion = currentSchemaVersion;
		std::string id;
		std::string name;
		bool enabled = true;
		RenderPassType type = RenderPassType::Custom;
		ExecutionMode executionMode = ExecutionMode::Automatic;
		PassOperation operation = PassOperation::Automatic;
		EventReferenceDisk event;
		std::string timing = timingBefore;
		ShaderResource::ResolutionPolicyDisk resolution;
		DispatchPolicyDisk dispatch;
		ViewportPolicyDisk viewport;
		std::vector<LogicalResourceBindingDisk> inputs;
		std::vector<LogicalResourceBindingDisk> outputs;
		std::vector<RuntimeResourceDefinitionDisk> runtimeResources;
		uint32_t sourceTextureShaderRegister = 0;
		uint32_t sourceTextureRegisterSpace = 0;
		bool trackResourceBindings = true;
		uint32_t maximumTrackedDescriptors = 64;
		InheritedGameBindingsDisk inheritedGameBindings;
		std::vector<ShaderResourceReferenceDisk> shaderResources;
		std::vector<SamplerStateDisk> samplers;
		std::string vertexShaderSourceFile;
		std::string fragmentShaderSourceFile;
		std::string vertexShaderCompiledBlobFile;
		std::string fragmentShaderCompiledBlobFile;
		std::string vertexShaderProfile = "vs_6_6";
		std::string fragmentShaderProfile = "ps_6_6";
		std::string vertexShaderEntryPoint = "main";
		std::string fragmentShaderEntryPoint = "main";

		//Runtime-only locations. The JSON file contains portable configuration only.
		std::string packageDirectory;
		std::string jsonPath;
		std::string vertexShaderSourcePath;
		std::string fragmentShaderSourcePath;
		std::string vertexShaderCompiledBlobPath;
		std::string fragmentShaderCompiledBlobPath;
		std::vector<uint8_t> vertexShaderBlob;
		std::vector<uint8_t> fragmentShaderBlob;
		uint64_t vertexShaderBlobHash = 0;
		uint64_t fragmentShaderBlobHash = 0;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			RenderPassDisk,
			format,
			schemaVersion,
			id,
			name,
			enabled,
			type,
			executionMode,
			operation,
			event,
			timing,
			resolution,
			dispatch,
			viewport,
			inputs,
			outputs,
			runtimeResources,
			sourceTextureShaderRegister,
			sourceTextureRegisterSpace,
			trackResourceBindings,
			maximumTrackedDescriptors,
			inheritedGameBindings,
			shaderResources,
			samplers,
			vertexShaderSourceFile,
			fragmentShaderSourceFile,
			vertexShaderCompiledBlobFile,
			fragmentShaderCompiledBlobFile,
			vertexShaderProfile,
			fragmentShaderProfile,
			vertexShaderEntryPoint,
			fragmentShaderEntryPoint)
	};
} //namespace RenderPass
