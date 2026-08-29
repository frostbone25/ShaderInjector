#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>

#include "JsonHelper.h"
#include "ShaderResource/ShaderResource.h"
namespace RenderPass
{
	inline constexpr const char* formatName = "ShaderInjector.RenderPass";
	inline constexpr int currentSchemaVersion = 8;
	inline constexpr const char* timingBefore = "Before";
	inline constexpr const char* timingAfter = "After";

	enum class RenderPassType
	{
		Custom,
		MipChain,
		ReplacementPixelShader,
		ReplacementComputeShader,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(RenderPassType,
	{
		{ RenderPassType::Custom, "Custom" },
		{ RenderPassType::MipChain, "MipChain" },
		{ RenderPassType::ReplacementPixelShader, "ReplacementPixelShader" },
		{ RenderPassType::ReplacementComputeShader, "ReplacementComputeShader" },
	})

	enum class ExecutionMode
	{
		Automatic,
		FullscreenPixel,
		Compute,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(ExecutionMode,
	{
		{ ExecutionMode::Automatic, "Automatic" },
		{ ExecutionMode::FullscreenPixel, "FullscreenPixel" },
		{ ExecutionMode::Compute, "Compute" },
	})

	enum class PassOperation
	{
		Automatic,
		Custom,
		ReplaceOriginal,
		MipChain,
		Downsample,
		UpsampleChain,
		Copy,
		Resolve,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(PassOperation,
	{
		{ PassOperation::Automatic, "Automatic" },
		{ PassOperation::Custom, "Custom" },
		{ PassOperation::ReplaceOriginal, "ReplaceOriginal" },
		{ PassOperation::MipChain, "MipChain" },
		{ PassOperation::Downsample, "Downsample" },
		{ PassOperation::UpsampleChain, "UpsampleChain" },
		{ PassOperation::Copy, "Copy" },
		{ PassOperation::Resolve, "Resolve" },
	})

	enum class DispatchMode
	{
		InheritOriginal,
		ScaleByResolution,
		ExplicitThreadGroups,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(DispatchMode,
	{
		{ DispatchMode::InheritOriginal, "InheritOriginal" },
		{ DispatchMode::ScaleByResolution, "ScaleByResolution" },
		{ DispatchMode::ExplicitThreadGroups, "ExplicitThreadGroups" },
	})

	enum class ViewportMode
	{
		InheritOriginal,
		MatchOutput,
		Explicit,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(ViewportMode,
	{
		{ ViewportMode::InheritOriginal, "InheritOriginal" },
		{ ViewportMode::MatchOutput, "MatchOutput" },
		{ ViewportMode::Explicit, "Explicit" },
	})

	enum class ResourceAccess
	{
		ShaderResource,
		UnorderedAccess,
		RenderTarget,
		DepthStencil,
		CopySource,
		CopyDestination,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(ResourceAccess,
	{
		{ ResourceAccess::ShaderResource, "ShaderResource" },
		{ ResourceAccess::UnorderedAccess, "UnorderedAccess" },
		{ ResourceAccess::RenderTarget, "RenderTarget" },
		{ ResourceAccess::DepthStencil, "DepthStencil" },
		{ ResourceAccess::CopySource, "CopySource" },
		{ ResourceAccess::CopyDestination, "CopyDestination" },
	})

	enum class GameResourceViewType
	{
		ShaderResource,
		UnorderedAccess,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(GameResourceViewType,
	{
		{ GameResourceViewType::ShaderResource, "ShaderResource" },
		{ GameResourceViewType::UnorderedAccess, "UnorderedAccess" },
	})

	struct DispatchPolicyDisk
	{
		DispatchMode mode = DispatchMode::InheritOriginal;
		uint32_t threadGroupSizeX = 8;
		uint32_t threadGroupSizeY = 8;
		uint32_t threadGroupSizeZ = 1;
		uint32_t explicitGroupCountX = 0;
		uint32_t explicitGroupCountY = 0;
		uint32_t explicitGroupCountZ = 0;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			DispatchPolicyDisk,
			mode,
			threadGroupSizeX,
			threadGroupSizeY,
			threadGroupSizeZ,
			explicitGroupCountX,
			explicitGroupCountY,
			explicitGroupCountZ)
	};

	struct ViewportPolicyDisk
	{
		ViewportMode mode = ViewportMode::InheritOriginal;
		uint32_t width = 0;
		uint32_t height = 0;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			ViewportPolicyDisk,
			mode,
			width,
			height)
	};

	struct LogicalResourceBindingDisk
	{
		std::string resourceId;
		std::string hlslName;
		ShaderResource::ResourceOrigin origin = ShaderResource::ResourceOrigin::Runtime;
		ResourceAccess access = ResourceAccess::ShaderResource;
		GameResourceViewType gameResourceViewType = GameResourceViewType::UnorderedAccess;
		ShaderResource::TemporalView temporalView = ShaderResource::TemporalView::Current;
		uint32_t shaderRegister = 0;
		uint32_t registerSpace = 0;
		bool optional = false;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			LogicalResourceBindingDisk,
			resourceId,
			hlslName,
			origin,
			access,
			gameResourceViewType,
			temporalView,
			shaderRegister,
			registerSpace,
			optional)
	};

	struct RuntimeResourceDefinitionDisk
	{
		std::string id;
		std::string name;
		ShaderResource::TextureDescriptionDisk texture;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			RuntimeResourceDefinitionDisk,
			id,
			name,
			texture)
	};

	struct ShaderResourceReferenceDisk
	{
		std::string resourceId;
		std::string hlslName;
		uint32_t shaderRegister = 0;
		uint32_t registerSpace = 0;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			ShaderResourceReferenceDisk,
			resourceId,
			hlslName,
			shaderRegister,
			registerSpace)
	};

	struct InheritedGameBindingsDisk
	{
		// Preserve the resource contract of the Modified Shader that anchors this
		// pass. The two categories can be disabled independently for deliberately
		// self-contained passes.
		bool shaderResources = true;
		bool constantBuffers = true;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			InheritedGameBindingsDisk,
			shaderResources,
			constantBuffers)
	};

	enum class EventType
	{
		ModifiedShader,
		RenderPass,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(EventType,
	{
		{ EventType::ModifiedShader, "ModifiedShader" },
		{ EventType::RenderPass, "RenderPass" },
	})

	struct EventReferenceDisk
	{
		EventType type = EventType::ModifiedShader;
		std::string id;

		NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
			EventReferenceDisk,
			type,
			id)
	};

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
		std::string vertexShaderSourceFile;
		std::string fragmentShaderSourceFile;
		std::string vertexShaderCompiledBlobFile;
		std::string fragmentShaderCompiledBlobFile;
		std::string vertexShaderProfile = "vs_6_6";
		std::string fragmentShaderProfile = "ps_6_6";
		std::string vertexShaderEntryPoint = "main";
		std::string fragmentShaderEntryPoint = "main";

		// Runtime-only locations. The JSON file contains portable configuration only.
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
			vertexShaderSourceFile,
			fragmentShaderSourceFile,
			vertexShaderCompiledBlobFile,
			fragmentShaderCompiledBlobFile,
			vertexShaderProfile,
			fragmentShaderProfile,
			vertexShaderEntryPoint,
			fragmentShaderEntryPoint)
	};

	struct ResourceBindingDiagnostic
	{
		std::string pipeline;
		std::string bindingType;
		uint32_t rootParameterIndex = UINT32_MAX;
		uint64_t gpuAddress = 0;
		uint64_t gpuDescriptorHandle = 0;
		uint64_t cpuDescriptorHandle = 0;
		uint32_t descriptorHeapType = UINT32_MAX;
		uint32_t descriptorIndex = UINT32_MAX;
		uint32_t descriptorCount = 0;
		uint32_t descriptorViewDimension = UINT32_MAX;
		uint32_t descriptorMostDetailedMip = 0;
		uint32_t descriptorMipLevels = 0;
		uint32_t descriptorShader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		uint32_t descriptorPlaneSlice = 0;
		float descriptorResourceMinLodClamp = 0.0f;
		uint32_t shaderRegister = UINT32_MAX;
		uint32_t registerSpace = UINT32_MAX;
		uint32_t destinationOffset = 0;
		uint64_t resourcePointer = 0;
		std::string resourceName;
		uint32_t resourceDimension = UINT32_MAX;
		uint64_t resourceWidth = 0;
		uint32_t resourceHeight = 0;
		uint32_t resourceDepthOrArraySize = 0;
		uint32_t resourceMipLevels = 0;
		uint32_t resourceFormat = 0;
		uint32_t resourceSampleCount = 0;
		uint32_t resourceSampleQuality = 0;
		uint64_t bufferOffset = 0;
		uint64_t bufferSize = 0;
		uint64_t firstElement = 0;
		uint32_t elementCount = 0;
		uint32_t structureByteStride = 0;
		std::vector<uint32_t> rootConstants;
	};

	struct RuntimeDiagnostics
	{
		uint64_t triggerCount = 0;
		uint64_t executionCount = 0;
		uint64_t executionFailureCount = 0;
		std::string lastTiming;
		std::string lastOperation;
		std::string lastExecutionError;
		std::string lastEventType;
		std::string lastEventId;
		std::string lastModifiedShaderId;
		std::string lastShaderTargetName;
		std::string lastShaderTargetHash;
		bool resourceSnapshotCaptured = false;
		std::vector<ResourceBindingDiagnostic> resourceBindings;
	};

	bool WriteJson(const RenderPassDisk& renderPass);
	bool LoadJson(const std::string& jsonPath, RenderPassDisk& outRenderPass);
	bool IsTimingValid(const std::string& timing);
	void ResolveShaderPaths(RenderPassDisk& renderPass);
	bool LoadCompiledShaderBlobs(RenderPassDisk& renderPass);
	const char* TypeName(RenderPassType type);
	const char* ExecutionModeName(ExecutionMode mode);
	const char* PassOperationName(PassOperation operation);
	const char* EventTypeName(EventType type);
	ExecutionMode ResolveExecutionMode(const RenderPassDisk& renderPass);
	PassOperation ResolvePassOperation(const RenderPassDisk& renderPass);
	const LogicalResourceBindingDisk* FindMipChainRuntimeSource(const RenderPassDisk& renderPass);
	std::string MipChainOutputResourceId(const RenderPassDisk& renderPass);
	void NormalizeExecutionResources(RenderPassDisk& renderPass);
	bool HasShaderTemplate(const RenderPassDisk& renderPass);
	bool HasCompiledShaders(const RenderPassDisk& renderPass);
	bool IsReplacementPass(RenderPassType type);
}
