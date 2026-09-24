//HookD3D12PipelineUtils.cpp

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <sstream>
#include <dxgi1_6.h>

//custom
#include "Hash/Hash.h"
#include "ShaderModelDetector.h"
#include "IO/ShaderInjectorIO.h"
#include "ShaderInjectorGUI.h"
#include "HookD3D12.h"
#include "RenderPassRuntime.h"
#include "StringHelper.h"

namespace HookD3D12
{
	//an absent stage has no hash or bytecode length in a portable pipeline signature.
	std::string FormatOptionalShaderHash(uint64_t shaderHash)
	{
		if (shaderHash)
			return Hash::FormatHash(shaderHash);
		return "";
	}

	std::string FormatOptionalShaderLength(SIZE_T bytecodeSize)
	{
		if (bytecodeSize)
			return std::to_string(static_cast<size_t>(bytecodeSize));
		return "";
	}

	void FillCommonReplacementHashes(ShaderTarget::ShaderTargetDisk& replacement, uint64_t vertexShaderHash, uint64_t pixelShaderHash, uint64_t computeShaderHash, uint64_t geometryShaderHash, uint64_t hullShaderHash, uint64_t domainShaderHash)
	{
		replacement.vsHash = FormatOptionalShaderHash(vertexShaderHash);
		replacement.psHash = FormatOptionalShaderHash(pixelShaderHash);
		replacement.csHash = FormatOptionalShaderHash(computeShaderHash);
		replacement.gsHash = FormatOptionalShaderHash(geometryShaderHash);
		replacement.hsHash = FormatOptionalShaderHash(hullShaderHash);
		replacement.dsHash = FormatOptionalShaderHash(domainShaderHash);
	}

	void FillCommonReplacementStageLengths(ShaderTarget::ShaderTargetDisk& replacement, SIZE_T vertexShaderBytecodeSize, SIZE_T pixelShaderBytecodeSize, SIZE_T computeShaderBytecodeSize, SIZE_T geometryShaderBytecodeSize, SIZE_T hullShaderBytecodeSize, SIZE_T domainShaderBytecodeSize)
	{
		replacement.vsLength = FormatOptionalShaderLength(vertexShaderBytecodeSize);
		replacement.psLength = FormatOptionalShaderLength(pixelShaderBytecodeSize);
		replacement.csLength = FormatOptionalShaderLength(computeShaderBytecodeSize);
		replacement.gsLength = FormatOptionalShaderLength(geometryShaderBytecodeSize);
		replacement.hsLength = FormatOptionalShaderLength(hullShaderBytecodeSize);
		replacement.dsLength = FormatOptionalShaderLength(domainShaderBytecodeSize);
	}

	std::string HashStructText(const void* data, size_t size)
	{
		if (data && size)
			return Hash::FormatHash(Hash::HashMemory(data, size));
		return "";
	}

	uint64_t CanonicalPipelineFixedFunctionStateHash(const std::vector<uint8_t>& streamBlob)
	{
		if (streamBlob.empty())
			return 0;

		std::vector<uint8_t> canonicalStream = streamBlob;
		uint8_t* streamPosition = canonicalStream.data();
		uint8_t* streamEnd = streamPosition + canonicalStream.size();

		while (streamPosition < streamEnd)
		{
			if (streamPosition + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) > streamEnd)
				return 0;

			const auto type = *reinterpret_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(streamPosition);
			const UINT typeIndex = static_cast<UINT>(type);

			if (typeIndex >= ARRAYSIZE(subobjectSizes) || subobjectSizes[typeIndex] == 0)
				return 0;

			const size_t subobjectSize = subobjectSizes[typeIndex];

			if (streamPosition + subobjectSize > streamEnd)
				return 0;

			//Pointer-bearing payloads and the driver cache are process-local. Removing
			//them leaves the blend/raster/depth/formats/topology state that identifies
			//distinct PSO variants of the same shader.
			switch (type)
			{
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE:
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT:
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT:
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO:
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING:
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS:
					std::memset(
						streamPosition + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE),
						0,
						subobjectSize - sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE));
					break;
				default:
					break;
			}

			streamPosition += subobjectSize;
		}

		return Hash::HashMemory(canonicalStream.data(), canonicalStream.size());
	}

	std::string JoinUIntValues(const UINT* values, UINT count)
	{
		std::ostringstream stream;

		for (UINT i = 0; i < count; ++i)
		{
			if (i > 0)
				stream << ",";

			stream << values[i];
		}

		return stream.str();
	}

	std::string RenderTargetFormatsSignature(const DXGI_FORMAT* formats, UINT count)
	{
		std::ostringstream stream;

		for (UINT i = 0; i < count; ++i)
		{
			if (i > 0)
				stream << ",";

			stream << (UINT)formats[i];
		}

		return stream.str();
	}

	std::string InputLayoutSignature(const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputElements)
	{
		std::ostringstream stream;

		for (size_t i = 0; i < inputElements.size(); ++i)
		{
			const D3D12_INPUT_ELEMENT_DESC& element = inputElements[i];

			if (i > 0)
				stream << ";";

			stream << StringHelper::SafeString(element.SemanticName) << ":"
				   << element.SemanticIndex << ":"
				   << (UINT)element.Format << ":"
				   << element.InputSlot << ":"
				   << element.AlignedByteOffset << ":"
				   << (UINT)element.InputSlotClass << ":"
				   << element.InstanceDataStepRate;
		}

		return stream.str();
	}

	std::string StreamOutputSignature(const std::vector<D3D12_SO_DECLARATION_ENTRY>& declarations, const std::vector<UINT>& strides)
	{
		std::ostringstream stream;

		for (size_t i = 0; i < declarations.size(); ++i)
		{
			const D3D12_SO_DECLARATION_ENTRY& entry = declarations[i];

			if (i > 0)
				stream << ";";

			stream << StringHelper::SafeString(entry.SemanticName) << ":"
				   << entry.SemanticIndex << ":"
				   << entry.StartComponent << ":"
				   << entry.ComponentCount << ":"
				   << entry.OutputSlot;
		}

		stream << "|strides=";

		for (size_t i = 0; i < strides.size(); ++i)
		{
			if (i > 0)
				stream << ",";

			stream << strides[i];
		}

		return stream.str();
	}

	std::string PipelineStreamSubobjectTypeSignature(const std::vector<uint8_t>& streamBlob)
	{
		if (streamBlob.empty())
			return "";

		const uint8_t* ptr = streamBlob.data();
		const uint8_t* end = ptr + streamBlob.size();
		std::ostringstream stream;
		bool first = true;

		while (ptr < end)
		{
			if (ptr + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) > end)
				break;

			auto type = *reinterpret_cast<const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(ptr);
			UINT typeIdx = (UINT)type;

			if (typeIdx >= ARRAYSIZE(subobjectSizes) || subobjectSizes[typeIdx] == 0)
				break;

			const size_t subobjectSize = subobjectSizes[typeIdx];

			if (ptr + subobjectSize > end)
				break;

			if (!first)
				stream << ",";

			stream << typeIdx;
			first = false;
			ptr += subobjectSize;
		}

		return stream.str();
	}

	void FillInputAndStreamOutputSignatures(ShaderTarget::ShaderTargetDisk& replacement, const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputElements, const std::vector<D3D12_SO_DECLARATION_ENTRY>& streamOutputDeclarations, const std::vector<UINT>& streamOutputStrides)
	{
		replacement.inputLayoutElementCount = std::to_string(inputElements.size());
		replacement.inputLayoutSignature = InputLayoutSignature(inputElements);
		replacement.streamOutputDeclarationCount = std::to_string(streamOutputDeclarations.size());
		replacement.streamOutputSignature = StreamOutputSignature(streamOutputDeclarations, streamOutputStrides);
	}

	void FillGraphicsReplacementPortableState(ShaderTarget::ShaderTargetDisk& replacement, const GraphicsPipelineInfo& pipeline)
	{
		FillCommonReplacementStageLengths(replacement, pipeline.vertexShaderBytecodeSize, pipeline.pixelShaderBytecodeSize, 0, pipeline.geometryShaderBytecodeSize, pipeline.hullShaderBytecodeSize, pipeline.domainShaderBytecodeSize);
		FillInputAndStreamOutputSignatures(replacement, pipeline.inputElements, pipeline.streamOutputDeclarations, pipeline.streamOutputStrides);

		replacement.renderTargetFormat0 = std::to_string((UINT)pipeline.originalDescription.RTVFormats[0]);
		replacement.renderTargetFormats = RenderTargetFormatsSignature(pipeline.originalDescription.RTVFormats, pipeline.originalDescription.NumRenderTargets);
		replacement.numRenderTargets = std::to_string(pipeline.originalDescription.NumRenderTargets);
		replacement.depthStencilFormat = std::to_string((UINT)pipeline.originalDescription.DSVFormat);
		replacement.primitiveTopologyType = std::to_string((UINT)pipeline.originalDescription.PrimitiveTopologyType);
		replacement.sampleCount = std::to_string(pipeline.originalDescription.SampleDesc.Count);
		replacement.sampleQuality = std::to_string(pipeline.originalDescription.SampleDesc.Quality);
		replacement.sampleMask = std::to_string(pipeline.originalDescription.SampleMask);
		replacement.blendStateHash = HashStructText(&pipeline.originalDescription.BlendState, sizeof(pipeline.originalDescription.BlendState));
		replacement.rasterizerStateHash = HashStructText(&pipeline.originalDescription.RasterizerState, sizeof(pipeline.originalDescription.RasterizerState));
		replacement.depthStencilStateHash = HashStructText(&pipeline.originalDescription.DepthStencilState, sizeof(pipeline.originalDescription.DepthStencilState));
	}

	void FillStreamReplacementPortableStateFromBlob(ShaderTarget::ShaderTargetDisk& replacement, const PipelineStateInfo& pipeline)
	{
		FillCommonReplacementStageLengths(replacement, pipeline.vertexShaderBytecodeSize, pipeline.pixelShaderBytecodeSize, pipeline.computeShaderBytecodeSize, pipeline.geometryShaderBytecodeSize, pipeline.hullShaderBytecodeSize, pipeline.domainShaderBytecodeSize);
		replacement.asLength = FormatOptionalShaderLength(pipeline.amplificationShaderBytecodeSize);
		replacement.msLength = FormatOptionalShaderLength(pipeline.meshShaderBytecodeSize);
		replacement.asHash = FormatOptionalShaderHash(pipeline.amplificationShaderHash);
		replacement.msHash = FormatOptionalShaderHash(pipeline.meshShaderHash);
		FillInputAndStreamOutputSignatures(replacement, pipeline.inputElements, pipeline.streamOutputDeclarations, pipeline.streamOutputStrides);
		replacement.pipelineStreamLength.clear();

		if (!pipeline.streamBlob.empty())
			replacement.pipelineStreamLength = std::to_string(pipeline.streamBlob.size());

		replacement.pipelineStreamSubobjectTypes = PipelineStreamSubobjectTypeSignature(pipeline.streamBlob);
		const uint64_t fixedFunctionStateHash = CanonicalPipelineFixedFunctionStateHash(pipeline.streamBlob);
		replacement.pipelineFixedFunctionStateHash = FormatOptionalShaderHash(fixedFunctionStateHash);

		if (pipeline.streamBlob.empty())
			return;

		const uint8_t* ptr = pipeline.streamBlob.data();
		const uint8_t* end = ptr + pipeline.streamBlob.size();

		while (ptr && ptr < end)
		{
			if (ptr + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) > end)
				break;

			auto type = *reinterpret_cast<const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(ptr);
			UINT typeIdx = (UINT)type;

			if (typeIdx >= ARRAYSIZE(subobjectSizes) || subobjectSizes[typeIdx] == 0)
				break;

			const size_t subobjectSize = subobjectSizes[typeIdx];

			if (ptr + subobjectSize > end)
				break;

			switch (type)
			{
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND:
				{
					const auto* subobject = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, D3D12_BLEND_DESC>*>(ptr);
					replacement.blendStateHash = HashStructText(&subobject->payloadData, sizeof(subobject->payloadData));
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:
				{
					const auto* subobject = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, UINT>*>(ptr);
					replacement.sampleMask = std::to_string(subobject->payloadData);
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER:
				{
					const auto* subobject = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, D3D12_RASTERIZER_DESC>*>(ptr);
					replacement.rasterizerStateHash = HashStructText(&subobject->payloadData, sizeof(subobject->payloadData));
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL:
				{
					const auto* subobject = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, D3D12_DEPTH_STENCIL_DESC>*>(ptr);
					replacement.depthStencilStateHash = HashStructText(&subobject->payloadData, sizeof(subobject->payloadData));
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:
				{
					const auto* subobject = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE>*>(ptr);
					replacement.primitiveTopologyType = std::to_string((UINT)subobject->payloadData);
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS:
				{
					const auto* subobject = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY>*>(ptr);
					const D3D12_RT_FORMAT_ARRAY& formats = subobject->payloadData;
					replacement.numRenderTargets = std::to_string(formats.NumRenderTargets);
					replacement.renderTargetFormat0.clear();

					if (formats.NumRenderTargets > 0)
						replacement.renderTargetFormat0 = std::to_string((UINT)formats.RTFormats[0]);

					replacement.renderTargetFormats = RenderTargetFormatsSignature(formats.RTFormats, formats.NumRenderTargets);
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT:
				{
					const auto* subobject = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT>*>(ptr);
					replacement.depthStencilFormat = std::to_string((UINT)subobject->payloadData);
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC:
				{
					const auto* subobject = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, DXGI_SAMPLE_DESC>*>(ptr);
					replacement.sampleCount = std::to_string(subobject->payloadData.Count);
					replacement.sampleQuality = std::to_string(subobject->payloadData.Quality);
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1:
				{
					const auto* subobject = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1, D3D12_DEPTH_STENCIL_DESC1>*>(ptr);
					replacement.depthStencilStateHash = HashStructText(&subobject->payloadData, sizeof(subobject->payloadData));
					break;
				}
				default:
					break;
			}

			ptr += subobjectSize;
		}
	}

	RenderPassRuntime::PipelineOutputState ExtractPipelineOutputState(const PipelineStateInfo& pipeline)
	{
		RenderPassRuntime::PipelineOutputState outputState{};

		if (pipeline.streamBlob.empty())
			return outputState;

		const uint8_t* streamPosition = pipeline.streamBlob.data();
		const uint8_t* streamEnd = streamPosition + pipeline.streamBlob.size();

		while (streamPosition && streamPosition < streamEnd)
		{
			if (streamPosition + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) > streamEnd)
				break;

			const auto type = *reinterpret_cast<const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(streamPosition);
			const UINT typeIndex = static_cast<UINT>(type);

			if (typeIndex >= ARRAYSIZE(subobjectSizes) || subobjectSizes[typeIndex] == 0)
				break;

			const size_t subobjectSize = subobjectSizes[typeIndex];

			if (streamPosition + subobjectSize > streamEnd)
				break;

			switch (type)
			{
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS:
				{
					const auto* subobject = reinterpret_cast<const PSOSubobject<
						D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,
						D3D12_RT_FORMAT_ARRAY>*>(streamPosition);

					outputState.renderTargetCount = (std::min)(
						subobject->payloadData.NumRenderTargets,
						static_cast<UINT>(D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT));

					for (UINT index = 0; index < outputState.renderTargetCount; ++index)
						outputState.renderTargetFormats[index] = subobject->payloadData.RTFormats[index];

					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT:
				{
					const auto* subobject = reinterpret_cast<const PSOSubobject<
						D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT,
						DXGI_FORMAT>*>(streamPosition);

					outputState.depthStencilFormat = subobject->payloadData;

					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC:
				{
					const auto* subobject = reinterpret_cast<const PSOSubobject<
						D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC,
						DXGI_SAMPLE_DESC>*>(streamPosition);

					outputState.sampleCount = 1;

					if (subobject->payloadData.Count)
						outputState.sampleCount = subobject->payloadData.Count;

					outputState.sampleQuality = subobject->payloadData.Quality;

					break;
				}
				default:
					break;
			}

			streamPosition += subobjectSize;
		}

		return outputState;
	}

	ShaderTarget::ShaderPipelineStreamMetadataDisk BuildPipelineStreamMetadata(const PipelineStateInfo& pipeline)
	{
		ShaderTarget::ShaderPipelineStreamMetadataDisk metadata{};

		for (const D3D12_INPUT_ELEMENT_DESC& element : pipeline.inputElements)
		{
			ShaderTarget::ShaderInputElementDisk diskElement{};
			diskElement.semanticName = StringHelper::SafeString(element.SemanticName);
			diskElement.semanticIndex = element.SemanticIndex;
			diskElement.format = (uint32_t)element.Format;
			diskElement.inputSlot = element.InputSlot;
			diskElement.alignedByteOffset = element.AlignedByteOffset;
			diskElement.inputSlotClass = (uint32_t)element.InputSlotClass;
			diskElement.instanceDataStepRate = element.InstanceDataStepRate;
			metadata.inputElements.push_back(diskElement);
		}

		for (const D3D12_SO_DECLARATION_ENTRY& entry : pipeline.streamOutputDeclarations)
		{
			ShaderTarget::ShaderStreamOutputDeclarationDisk diskEntry{};
			diskEntry.semanticName = StringHelper::SafeString(entry.SemanticName);
			diskEntry.semanticIndex = entry.SemanticIndex;
			diskEntry.startComponent = entry.StartComponent;
			diskEntry.componentCount = entry.ComponentCount;
			diskEntry.outputSlot = entry.OutputSlot;
			metadata.streamOutputDeclarations.push_back(diskEntry);
		}

		for (UINT stride : pipeline.streamOutputStrides)
			metadata.streamOutputStrides.push_back(stride);

		metadata.hasViewInstancing = pipeline.hasViewInstancing;
		metadata.viewInstancingFlags = static_cast<uint32_t>(pipeline.viewInstancingFlags);

		for (const D3D12_VIEW_INSTANCE_LOCATION& location : pipeline.viewInstanceLocations)
		{
			metadata.viewInstanceViewportArrayIndices.push_back(location.ViewportArrayIndex);
			metadata.viewInstanceRenderTargetArrayIndices.push_back(location.RenderTargetArrayIndex);
		}

		return metadata;
	}

	void ApplyPipelineStreamMetadata(const ShaderTarget::ShaderPipelineStreamMetadataDisk& metadata, PipelineStateInfo& pipeline)
	{
		pipeline.inputElements.clear();
		pipeline.inputElementSemanticNames.clear();
		pipeline.streamOutputDeclarations.clear();
		pipeline.streamOutputSemanticNames.clear();
		pipeline.streamOutputStrides.clear();
		pipeline.hasViewInstancing = metadata.hasViewInstancing;
		pipeline.viewInstancingFlags = static_cast<D3D12_VIEW_INSTANCING_FLAGS>(metadata.viewInstancingFlags);
		pipeline.viewInstanceLocations.clear();

		pipeline.inputElements.reserve(metadata.inputElements.size());
		pipeline.inputElementSemanticNames.reserve(metadata.inputElements.size());

		for (const ShaderTarget::ShaderInputElementDisk& diskElement : metadata.inputElements)
		{
			pipeline.inputElementSemanticNames.push_back(diskElement.semanticName);

			D3D12_INPUT_ELEMENT_DESC element{};
			element.SemanticName = pipeline.inputElementSemanticNames.back().c_str();
			element.SemanticIndex = diskElement.semanticIndex;
			element.Format = (DXGI_FORMAT)diskElement.format;
			element.InputSlot = diskElement.inputSlot;
			element.AlignedByteOffset = diskElement.alignedByteOffset;
			element.InputSlotClass = (D3D12_INPUT_CLASSIFICATION)diskElement.inputSlotClass;
			element.InstanceDataStepRate = diskElement.instanceDataStepRate;
			pipeline.inputElements.push_back(element);
		}

		pipeline.streamOutputDeclarations.reserve(metadata.streamOutputDeclarations.size());
		pipeline.streamOutputSemanticNames.reserve(metadata.streamOutputDeclarations.size());

		for (const ShaderTarget::ShaderStreamOutputDeclarationDisk& diskEntry : metadata.streamOutputDeclarations)
		{
			pipeline.streamOutputSemanticNames.push_back(diskEntry.semanticName);

			D3D12_SO_DECLARATION_ENTRY entry{};
			entry.SemanticName = pipeline.streamOutputSemanticNames.back().c_str();
			entry.SemanticIndex = diskEntry.semanticIndex;
			entry.StartComponent = (BYTE)diskEntry.startComponent;
			entry.ComponentCount = (BYTE)diskEntry.componentCount;
			entry.OutputSlot = (BYTE)diskEntry.outputSlot;
			pipeline.streamOutputDeclarations.push_back(entry);
		}

		for (uint32_t stride : metadata.streamOutputStrides)
			pipeline.streamOutputStrides.push_back((UINT)stride);

		if (metadata.viewInstanceViewportArrayIndices.size() ==
			metadata.viewInstanceRenderTargetArrayIndices.size())
		{
			pipeline.viewInstanceLocations.reserve(metadata.viewInstanceViewportArrayIndices.size());

			for (size_t locationIndex = 0;
				 locationIndex < metadata.viewInstanceViewportArrayIndices.size();
				 ++locationIndex)
			{
				D3D12_VIEW_INSTANCE_LOCATION location{};
				location.ViewportArrayIndex = metadata.viewInstanceViewportArrayIndices[locationIndex];
				location.RenderTargetArrayIndex = metadata.viewInstanceRenderTargetArrayIndices[locationIndex];
				pipeline.viewInstanceLocations.push_back(location);
			}
		}
	}

	void RebindPipelineStateInfoPointerFields(PipelineStateInfo& info)
	{
		if (info.inputElementSemanticNames.size() == info.inputElements.size())
		{
			for (size_t i = 0; i < info.inputElements.size(); ++i)
				info.inputElements[i].SemanticName = info.inputElementSemanticNames[i].c_str();
		}

		if (info.streamOutputSemanticNames.size() == info.streamOutputDeclarations.size())
		{
			for (size_t i = 0; i < info.streamOutputDeclarations.size(); ++i)
				info.streamOutputDeclarations[i].SemanticName = info.streamOutputSemanticNames[i].c_str();
		}

		if (!info.hasViewInstancing || info.streamBlob.empty())
			return;

		uint8_t* streamPointer = info.streamBlob.data();
		uint8_t* streamEnd = streamPointer + info.streamBlob.size();

		while (streamPointer < streamEnd)
		{
			if (streamPointer + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) > streamEnd)
				return;

			const auto type = *reinterpret_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(streamPointer);
			const UINT typeIndex = static_cast<UINT>(type);

			if (typeIndex >= ARRAYSIZE(subobjectSizes) || subobjectSizes[typeIndex] == 0)
				return;

			const size_t subobjectSize = subobjectSizes[typeIndex];

			if (streamPointer + subobjectSize > streamEnd)
				return;

			if (type == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING)
			{
				auto* viewInstancing = reinterpret_cast<D3D12_VIEW_INSTANCING_DESC*>(streamPointer + sizeof(void*));
				viewInstancing->ViewInstanceCount = static_cast<UINT>(info.viewInstanceLocations.size());
				viewInstancing->pViewInstanceLocations = nullptr;

				if (!info.viewInstanceLocations.empty())
					viewInstancing->pViewInstanceLocations = info.viewInstanceLocations.data();

				viewInstancing->Flags = info.viewInstancingFlags;
				return;
			}

			streamPointer += subobjectSize;
		}
	}

	void ParsePipelineStream(const D3D12_PIPELINE_STATE_STREAM_DESC* desc, PipelineStateInfo& info)
	{
		if (!desc || !desc->pPipelineStateSubobjectStream || desc->SizeInBytes == 0)
			return;

		const uint8_t* ptr = static_cast<const uint8_t*>(desc->pPipelineStateSubobjectStream);
		const uint8_t* end = ptr + desc->SizeInBytes;

		while (ptr < end)
		{
			if (ptr + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) > end)
				break;

			auto type = *reinterpret_cast<const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(ptr);
			UINT typeIdx = (UINT)type;

			if (typeIdx >= ARRAYSIZE(subobjectSizes) || subobjectSizes[typeIdx] == 0)
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12PipelineUtils->ParsePipelineStream: unknown subobject type " + std::to_string(typeIdx) + ", cannot continue");
				return;
			}

			size_t subobjectSize = subobjectSizes[typeIdx];

			if (ptr + subobjectSize > end)
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12PipelineUtils->ParsePipelineStream: subobject overruns stream buffer, aborting");
				return;
			}

			switch (type)
			{
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE:
				{
					auto* subobj = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature*>*>(ptr);
					info.rootSignature = subobj->payloadData;
					break;
				}

				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
				{
					auto* subobj = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, D3D12_SHADER_BYTECODE>*>(ptr);

					if (subobj->payloadData.pShaderBytecode && subobj->payloadData.BytecodeLength)
					{
						ShaderModelDetector::ObserveShaderBytecode(ShaderTarget::VertexShader, subobj->payloadData.pShaderBytecode, subobj->payloadData.BytecodeLength);
						info.vertexShaderHash = Hash::HashMemory(subobj->payloadData.pShaderBytecode, subobj->payloadData.BytecodeLength);
						info.vertexShaderBytecodeSize = subobj->payloadData.BytecodeLength;
						info.vertexShaderBytecode.assign((const uint8_t*)subobj->payloadData.pShaderBytecode, (const uint8_t*)subobj->payloadData.pShaderBytecode + subobj->payloadData.BytecodeLength);
					}

					info.isGraphics = true;
					break;
				}

				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
				{
					auto* subobj = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE>*>(ptr);

					if (subobj->payloadData.pShaderBytecode && subobj->payloadData.BytecodeLength)
					{
						ShaderModelDetector::ObserveShaderBytecode(ShaderTarget::PixelShader, subobj->payloadData.pShaderBytecode, subobj->payloadData.BytecodeLength);
						info.pixelShaderHash = Hash::HashMemory(subobj->payloadData.pShaderBytecode, subobj->payloadData.BytecodeLength);
						info.pixelShaderBytecodeSize = subobj->payloadData.BytecodeLength;
						info.pixelShaderBytecode.assign((const uint8_t*)subobj->payloadData.pShaderBytecode, (const uint8_t*)subobj->payloadData.pShaderBytecode + subobj->payloadData.BytecodeLength);
					}

					info.isGraphics = true;
					break;
				}

				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
				{
					auto* subobj = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS, D3D12_SHADER_BYTECODE>*>(ptr);

					if (subobj->payloadData.pShaderBytecode && subobj->payloadData.BytecodeLength)
					{
						ShaderModelDetector::ObserveShaderBytecode(ShaderTarget::GeometryShader, subobj->payloadData.pShaderBytecode, subobj->payloadData.BytecodeLength);
						info.geometryShaderHash = Hash::HashMemory(subobj->payloadData.pShaderBytecode, subobj->payloadData.BytecodeLength);
						info.geometryShaderBytecodeSize = subobj->payloadData.BytecodeLength;
						info.geometryShaderBytecode.assign((const uint8_t*)subobj->payloadData.pShaderBytecode, (const uint8_t*)subobj->payloadData.pShaderBytecode + subobj->payloadData.BytecodeLength);
					}

					info.isGraphics = true;
					break;
				}

				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
				{
					auto* subobj = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS, D3D12_SHADER_BYTECODE>*>(ptr);

					if (subobj->payloadData.pShaderBytecode && subobj->payloadData.BytecodeLength)
					{
						ShaderModelDetector::ObserveShaderBytecode(ShaderTarget::HullShader, subobj->payloadData.pShaderBytecode, subobj->payloadData.BytecodeLength);
						info.hullShaderHash = Hash::HashMemory(subobj->payloadData.pShaderBytecode, subobj->payloadData.BytecodeLength);
						info.hullShaderBytecodeSize = subobj->payloadData.BytecodeLength;
						info.hullShaderBytecode.assign((const uint8_t*)subobj->payloadData.pShaderBytecode, (const uint8_t*)subobj->payloadData.pShaderBytecode + subobj->payloadData.BytecodeLength);
					}

					info.isGraphics = true;
					break;
				}

				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
				{
					auto* subobj = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS, D3D12_SHADER_BYTECODE>*>(ptr);

					if (subobj->payloadData.pShaderBytecode && subobj->payloadData.BytecodeLength)
					{
						ShaderModelDetector::ObserveShaderBytecode(ShaderTarget::DomainShader, subobj->payloadData.pShaderBytecode, subobj->payloadData.BytecodeLength);
						info.domainShaderHash = Hash::HashMemory(subobj->payloadData.pShaderBytecode, subobj->payloadData.BytecodeLength);
						info.domainShaderBytecodeSize = subobj->payloadData.BytecodeLength;
						info.domainShaderBytecode.assign((const uint8_t*)subobj->payloadData.pShaderBytecode, (const uint8_t*)subobj->payloadData.pShaderBytecode + subobj->payloadData.BytecodeLength);
					}

					info.isGraphics = true;
					break;
				}

				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
				{
					auto* subobj = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS, D3D12_SHADER_BYTECODE>*>(ptr);

					if (subobj->payloadData.pShaderBytecode && subobj->payloadData.BytecodeLength)
					{
						ShaderModelDetector::ObserveShaderBytecode(ShaderTarget::ComputeShader, subobj->payloadData.pShaderBytecode, subobj->payloadData.BytecodeLength);
						info.computeShaderHash = Hash::HashMemory(subobj->payloadData.pShaderBytecode, subobj->payloadData.BytecodeLength);
						info.computeShaderBytecodeSize = subobj->payloadData.BytecodeLength;
						info.computeShaderBytecode.assign((const uint8_t*)subobj->payloadData.pShaderBytecode, (const uint8_t*)subobj->payloadData.pShaderBytecode + subobj->payloadData.BytecodeLength);
					}

					info.isCompute = true;
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
				{
					auto* subobj = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS, D3D12_SHADER_BYTECODE>*>(ptr);

					if (subobj->payloadData.pShaderBytecode && subobj->payloadData.BytecodeLength)
					{
						info.amplificationShaderHash = Hash::HashMemory(subobj->payloadData.pShaderBytecode, subobj->payloadData.BytecodeLength);
						info.amplificationShaderBytecodeSize = subobj->payloadData.BytecodeLength;
						info.amplificationShaderBytecode.assign((const uint8_t*)subobj->payloadData.pShaderBytecode, (const uint8_t*)subobj->payloadData.pShaderBytecode + subobj->payloadData.BytecodeLength);
					}

					info.isGraphics = true;
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS:
				{
					auto* subobj = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, D3D12_SHADER_BYTECODE>*>(ptr);

					if (subobj->payloadData.pShaderBytecode && subobj->payloadData.BytecodeLength)
					{
						info.meshShaderHash = Hash::HashMemory(subobj->payloadData.pShaderBytecode, subobj->payloadData.BytecodeLength);
						info.meshShaderBytecodeSize = subobj->payloadData.BytecodeLength;
						info.meshShaderBytecode.assign((const uint8_t*)subobj->payloadData.pShaderBytecode, (const uint8_t*)subobj->payloadData.pShaderBytecode + subobj->payloadData.BytecodeLength);
					}

					info.isGraphics = true;
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT:
				{
					auto* subobj = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT, D3D12_INPUT_LAYOUT_DESC>*>(ptr);

					if (subobj->payloadData.pInputElementDescs && subobj->payloadData.NumElements > 0)
					{
						info.inputElements.assign(subobj->payloadData.pInputElementDescs, subobj->payloadData.pInputElementDescs + subobj->payloadData.NumElements);
						info.inputElementSemanticNames.clear();
						info.inputElementSemanticNames.reserve(info.inputElements.size());

						for (const D3D12_INPUT_ELEMENT_DESC& element : info.inputElements)
							info.inputElementSemanticNames.push_back(StringHelper::SafeString(element.SemanticName));

						for (size_t i = 0; i < info.inputElements.size(); ++i)
							info.inputElements[i].SemanticName = info.inputElementSemanticNames[i].c_str();
					}

					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT:
				{
					auto* subobj = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT, D3D12_STREAM_OUTPUT_DESC>*>(ptr);

					if (subobj->payloadData.pSODeclaration && subobj->payloadData.NumEntries > 0)
					{
						info.streamOutputDeclarations.assign(subobj->payloadData.pSODeclaration, subobj->payloadData.pSODeclaration + subobj->payloadData.NumEntries);
						info.streamOutputSemanticNames.clear();
						info.streamOutputSemanticNames.reserve(info.streamOutputDeclarations.size());

						for (const D3D12_SO_DECLARATION_ENTRY& entry : info.streamOutputDeclarations)
							info.streamOutputSemanticNames.push_back(StringHelper::SafeString(entry.SemanticName));

						for (size_t i = 0; i < info.streamOutputDeclarations.size(); ++i)
							info.streamOutputDeclarations[i].SemanticName = info.streamOutputSemanticNames[i].c_str();
					}

					if (subobj->payloadData.pBufferStrides && subobj->payloadData.NumStrides > 0)
					{
						info.streamOutputStrides.assign(subobj->payloadData.pBufferStrides, subobj->payloadData.pBufferStrides + subobj->payloadData.NumStrides);
					}

					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING:
				{
					auto* subobject = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING, D3D12_VIEW_INSTANCING_DESC>*>(ptr);

					info.hasViewInstancing = true;
					info.viewInstancingFlags = subobject->payloadData.Flags;

					if (subobject->payloadData.pViewInstanceLocations && subobject->payloadData.ViewInstanceCount > 0)
					{
						info.viewInstanceLocations.assign(
							subobject->payloadData.pViewInstanceLocations,
							subobject->payloadData.pViewInstanceLocations + subobject->payloadData.ViewInstanceCount);
					}

					break;
				}
				default:
					break;
			}

			ptr += subobjectSize;
		}
	}

	void GatherD3D12PipelineInfo(IDXGISwapChain3* swapChain, ID3D12Device* device, ID3D12CommandQueue* commandQueue, D3D12PipelineInfo& pipelineInfo)
	{
		if (!swapChain || !device)
			return;

		ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12PipelineUtils->GatherD3D12PipelineInfo: gathering pipeline info...");

		DXGI_SWAP_CHAIN_DESC desc{};
		swapChain->GetDesc(&desc);

		pipelineInfo.swapChainBufferCount = desc.BufferCount;
		pipelineInfo.swapChainFormat = desc.BufferDesc.Format;

		ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12PipelineUtils->GatherD3D12PipelineInfo: swapChainBuffers: " + std::to_string(pipelineInfo.swapChainBufferCount));
		ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12PipelineUtils->GatherD3D12PipelineInfo: swapChainFormat: " + std::to_string(pipelineInfo.swapChainFormat));

		IDXGIDevice* dxgiDevice = nullptr;

		if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))))
		{
			IDXGIAdapter* adapter = nullptr;

			if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter)))
			{
				DXGI_ADAPTER_DESC adapterDesc{};
				adapter->GetDesc(&adapterDesc);

				char graphicsProcessorName[256]{};
				wcstombs_s(nullptr, graphicsProcessorName, adapterDesc.Description, sizeof(graphicsProcessorName));

				pipelineInfo.graphicsProcessorName = graphicsProcessorName;
				pipelineInfo.vendorID = adapterDesc.VendorId;
				pipelineInfo.deviceID = adapterDesc.DeviceId;
				pipelineInfo.dedicatedVideoMemory = adapterDesc.DedicatedVideoMemory;
				pipelineInfo.dedicatedSystemMemory = adapterDesc.DedicatedSystemMemory;
				pipelineInfo.sharedSystemMemory = adapterDesc.SharedSystemMemory;

				adapter->Release();
			}

			dxgiDevice->Release();
		}
		else
		{
			IDXGIFactory6* factory = nullptr;
			HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));

			if (FAILED(hr))
			{
				pipelineInfo.graphicsProcessorName = "CreateDXGIFactory1 failed";
				return;
			}

			IDXGIAdapter1* adapter = nullptr;

			for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
			{
				DXGI_ADAPTER_DESC1 desc;
				adapter->GetDesc1(&desc);

				if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				{
					adapter->Release();
					continue;
				}

				char graphicsProcessorName[256];
				wcstombs_s(nullptr, graphicsProcessorName, sizeof(graphicsProcessorName), desc.Description, _TRUNCATE);

				pipelineInfo.graphicsProcessorName = graphicsProcessorName;
				pipelineInfo.vendorID = desc.VendorId;
				pipelineInfo.deviceID = desc.DeviceId;
				pipelineInfo.dedicatedVideoMemory = desc.DedicatedVideoMemory;
				pipelineInfo.dedicatedSystemMemory = desc.DedicatedSystemMemory;
				pipelineInfo.sharedSystemMemory = desc.SharedSystemMemory;

				adapter->Release();
				break;
			}

			factory->Release();
		}

		D3D12_FEATURE_DATA_D3D12_OPTIONS options{};

		if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
		{
			pipelineInfo.resourceBindingTier = options.ResourceBindingTier;
			pipelineInfo.tiledResourcesTier = options.TiledResourcesTier;
			pipelineInfo.conservativeRasterTier = options.ConservativeRasterizationTier;
		}

		D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};

		if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
		{
			pipelineInfo.raytracingTier = options5.RaytracingTier;
		}

		D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};

		if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7))))
		{
			pipelineInfo.meshShaderTier = options7.MeshShaderTier;
		}

		if (commandQueue)
		{
			D3D12_COMMAND_QUEUE_DESC queueDesc = commandQueue->GetDesc();
			pipelineInfo.commandQueueType = queueDesc.Type;
		}
	}
} //namespace HookD3D12
