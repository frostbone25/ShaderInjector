#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>

#include "JsonHelper.h"
namespace RenderPass
{
	inline constexpr const char* formatName = "ShaderInjector.RenderPass";
	inline constexpr int currentSchemaVersion = 1;
	inline constexpr const char* timingBefore = "Before";
	inline constexpr const char* timingAfter = "After";

	enum class RenderPassType
	{
		Custom,
		MipChain,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(RenderPassType,
	{
		{ RenderPassType::Custom, "Custom" },
		{ RenderPassType::MipChain, "MipChain" },
	})

	struct RenderPassDisk
	{
		std::string format = formatName;
		int schemaVersion = currentSchemaVersion;
		std::string id;
		std::string name;
		bool enabled = true;
		RenderPassType type = RenderPassType::Custom;
		std::string modifiedShaderId;
		std::string timing = timingBefore;
		uint32_t sourceTextureShaderRegister = 0;
		uint32_t sourceTextureRegisterSpace = 0;
		bool trackResourceBindings = true;
		uint32_t maximumTrackedDescriptors = 64;
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
			modifiedShaderId,
			timing,
			sourceTextureShaderRegister,
			sourceTextureRegisterSpace,
			trackResourceBindings,
			maximumTrackedDescriptors,
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
	bool HasShaderTemplate(const RenderPassDisk& renderPass);
	bool HasCompiledShaders(const RenderPassDisk& renderPass);
}
