#include "HookD3D12PipelineUtils.h"
#include "HookD3D12.h"

#include <cstdio>
#include <sstream>
#include <dxgi1_6.h>

#include "Hash.h"
#include "ShaderInjectorIO.h"
#include "ShaderInjectorGUI.h"

namespace HookD3D12
{
	std::string PointerToString(const void* ptr)
	{
		char buffer[32]{};
		sprintf_s(buffer, "%p", ptr);
		return buffer;
	}

	void FillCommonReplacementHashes(ShaderReplacement::ShaderReplacementDisk& replacement, uint64_t vsHash, uint64_t psHash, uint64_t csHash, uint64_t gsHash, uint64_t hsHash, uint64_t dsHash)
	{
		replacement.vsHash = vsHash ? Hash::FormatHash(vsHash) : "";
		replacement.psHash = psHash ? Hash::FormatHash(psHash) : "";
		replacement.csHash = csHash ? Hash::FormatHash(csHash) : "";
		replacement.gsHash = gsHash ? Hash::FormatHash(gsHash) : "";
		replacement.hsHash = hsHash ? Hash::FormatHash(hsHash) : "";
		replacement.dsHash = dsHash ? Hash::FormatHash(dsHash) : "";
	}

	void FillCommonReplacementStageLengths(ShaderReplacement::ShaderReplacementDisk& replacement, SIZE_T vsSize, SIZE_T psSize, SIZE_T csSize, SIZE_T gsSize, SIZE_T hsSize, SIZE_T dsSize)
	{
		replacement.vsLength = vsSize ? std::to_string((size_t)vsSize) : "";
		replacement.psLength = psSize ? std::to_string((size_t)psSize) : "";
		replacement.csLength = csSize ? std::to_string((size_t)csSize) : "";
		replacement.gsLength = gsSize ? std::to_string((size_t)gsSize) : "";
		replacement.hsLength = hsSize ? std::to_string((size_t)hsSize) : "";
		replacement.dsLength = dsSize ? std::to_string((size_t)dsSize) : "";
	}

	std::string HashStructText(const void* data, size_t size)
	{
		return data && size ? Hash::FormatHash(Hash::HashMemory(data, size)) : "";
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
			stream << (element.SemanticName ? element.SemanticName : "") << ":"
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
			stream << (entry.SemanticName ? entry.SemanticName : "") << ":"
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
			if (typeIdx >= ARRAYSIZE(kSubobjectSizes))
				break;

			const size_t subobjectSize = kSubobjectSizes[typeIdx];
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

	void FillInputAndStreamOutputSignatures(ShaderReplacement::ShaderReplacementDisk& replacement, const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputElements, const std::vector<D3D12_SO_DECLARATION_ENTRY>& soDeclarations, const std::vector<UINT>& soStrides)
	{
		replacement.inputLayoutElementCount = std::to_string(inputElements.size());
		replacement.inputLayoutSignature = InputLayoutSignature(inputElements);
		replacement.streamOutputDeclarationCount = std::to_string(soDeclarations.size());
		replacement.streamOutputSignature = StreamOutputSignature(soDeclarations, soStrides);
	}

	void FillGraphicsReplacementPortableState(ShaderReplacement::ShaderReplacementDisk& replacement, const GraphicsPipelineInfo& pipeline)
	{
		FillCommonReplacementStageLengths(replacement, pipeline.vsSize, pipeline.psSize, 0, pipeline.gsSize, pipeline.hsSize, pipeline.dsSize);
		FillInputAndStreamOutputSignatures(replacement, pipeline.inputElements, pipeline.soDeclarations, pipeline.soStrides);

		replacement.renderTargetFormat0 = std::to_string((UINT)pipeline.originalDesc.RTVFormats[0]);
		replacement.renderTargetFormats = RenderTargetFormatsSignature(pipeline.originalDesc.RTVFormats, pipeline.originalDesc.NumRenderTargets);
		replacement.numRenderTargets = std::to_string(pipeline.originalDesc.NumRenderTargets);
		replacement.depthStencilFormat = std::to_string((UINT)pipeline.originalDesc.DSVFormat);
		replacement.primitiveTopologyType = std::to_string((UINT)pipeline.originalDesc.PrimitiveTopologyType);
		replacement.sampleCount = std::to_string(pipeline.originalDesc.SampleDesc.Count);
		replacement.sampleQuality = std::to_string(pipeline.originalDesc.SampleDesc.Quality);
		replacement.sampleMask = std::to_string(pipeline.originalDesc.SampleMask);
		replacement.blendStateHash = HashStructText(&pipeline.originalDesc.BlendState, sizeof(pipeline.originalDesc.BlendState));
		replacement.rasterizerStateHash = HashStructText(&pipeline.originalDesc.RasterizerState, sizeof(pipeline.originalDesc.RasterizerState));
		replacement.depthStencilStateHash = HashStructText(&pipeline.originalDesc.DepthStencilState, sizeof(pipeline.originalDesc.DepthStencilState));
	}

	void FillStreamReplacementPortableStateFromBlob(ShaderReplacement::ShaderReplacementDisk& replacement, const PipelineStateInfo& pipeline)
	{
		FillCommonReplacementStageLengths(replacement, pipeline.vsSize, pipeline.psSize, pipeline.csSize, pipeline.gsSize, pipeline.hsSize, pipeline.dsSize);
		FillInputAndStreamOutputSignatures(replacement, pipeline.inputElements, pipeline.soDeclarations, pipeline.soStrides);
		replacement.pipelineStreamLength = pipeline.streamBlob.empty() ? "" : std::to_string(pipeline.streamBlob.size());
		replacement.pipelineStreamSubobjectTypes = PipelineStreamSubobjectTypeSignature(pipeline.streamBlob);

		const uint8_t* ptr = pipeline.streamBlob.empty() ? nullptr : pipeline.streamBlob.data();
		const uint8_t* end = ptr ? ptr + pipeline.streamBlob.size() : nullptr;

		while (ptr && ptr < end)
		{
			if (ptr + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) > end)
				break;

			auto type = *reinterpret_cast<const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(ptr);
			UINT typeIdx = (UINT)type;
			if (typeIdx >= ARRAYSIZE(kSubobjectSizes))
				break;

			const size_t subobjectSize = kSubobjectSizes[typeIdx];
			if (ptr + subobjectSize > end)
				break;

			const uint8_t* payloadPtr = ptr + sizeof(void*);
			switch (type)
			{
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND:
				{
					auto* desc = reinterpret_cast<const D3D12_BLEND_DESC*>(payloadPtr);
					replacement.blendStateHash = HashStructText(desc, sizeof(*desc));
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:
				{
					auto* sampleMask = reinterpret_cast<const UINT*>(payloadPtr);
					replacement.sampleMask = std::to_string(*sampleMask);
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER:
				{
					auto* desc = reinterpret_cast<const D3D12_RASTERIZER_DESC*>(payloadPtr);
					replacement.rasterizerStateHash = HashStructText(desc, sizeof(*desc));
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL:
				{
					auto* desc = reinterpret_cast<const D3D12_DEPTH_STENCIL_DESC*>(payloadPtr);
					replacement.depthStencilStateHash = HashStructText(desc, sizeof(*desc));
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:
				{
					auto* topology = reinterpret_cast<const D3D12_PRIMITIVE_TOPOLOGY_TYPE*>(payloadPtr);
					replacement.primitiveTopologyType = std::to_string((UINT)*topology);
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS:
				{
					auto* formats = reinterpret_cast<const D3D12_RT_FORMAT_ARRAY*>(payloadPtr);
					replacement.numRenderTargets = std::to_string(formats->NumRenderTargets);
					replacement.renderTargetFormat0 = formats->NumRenderTargets > 0 ? std::to_string((UINT)formats->RTFormats[0]) : "";
					replacement.renderTargetFormats = RenderTargetFormatsSignature(formats->RTFormats, formats->NumRenderTargets);
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT:
				{
					auto* format = reinterpret_cast<const DXGI_FORMAT*>(payloadPtr);
					replacement.depthStencilFormat = std::to_string((UINT)*format);
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC:
				{
					auto* sampleDesc = reinterpret_cast<const DXGI_SAMPLE_DESC*>(payloadPtr);
					replacement.sampleCount = std::to_string(sampleDesc->Count);
					replacement.sampleQuality = std::to_string(sampleDesc->Quality);
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1:
				{
					auto* desc = reinterpret_cast<const D3D12_DEPTH_STENCIL_DESC1*>(payloadPtr);
					replacement.depthStencilStateHash = HashStructText(desc, sizeof(*desc));
					break;
				}
				default:
					break;
			}

			ptr += subobjectSize;
		}
	}

	ShaderReplacement::ShaderPipelineStreamMetadataDisk BuildPipelineStreamMetadata(const PipelineStateInfo& pipeline)
	{
		ShaderReplacement::ShaderPipelineStreamMetadataDisk metadata{};

		for (const D3D12_INPUT_ELEMENT_DESC& element : pipeline.inputElements)
		{
			ShaderReplacement::ShaderInputElementDisk diskElement{};
			diskElement.semanticName = element.SemanticName ? element.SemanticName : "";
			diskElement.semanticIndex = element.SemanticIndex;
			diskElement.format = (uint32_t)element.Format;
			diskElement.inputSlot = element.InputSlot;
			diskElement.alignedByteOffset = element.AlignedByteOffset;
			diskElement.inputSlotClass = (uint32_t)element.InputSlotClass;
			diskElement.instanceDataStepRate = element.InstanceDataStepRate;
			metadata.inputElements.push_back(diskElement);
		}

		for (const D3D12_SO_DECLARATION_ENTRY& entry : pipeline.soDeclarations)
		{
			ShaderReplacement::ShaderStreamOutputDeclarationDisk diskEntry{};
			diskEntry.semanticName = entry.SemanticName ? entry.SemanticName : "";
			diskEntry.semanticIndex = entry.SemanticIndex;
			diskEntry.startComponent = entry.StartComponent;
			diskEntry.componentCount = entry.ComponentCount;
			diskEntry.outputSlot = entry.OutputSlot;
			metadata.streamOutputDeclarations.push_back(diskEntry);
		}

		for (UINT stride : pipeline.soStrides)
			metadata.streamOutputStrides.push_back(stride);

		return metadata;
	}

	void ApplyPipelineStreamMetadata(const ShaderReplacement::ShaderPipelineStreamMetadataDisk& metadata, PipelineStateInfo& pipeline)
	{
		pipeline.inputElements.clear();
		pipeline.inputElementSemanticNames.clear();
		pipeline.soDeclarations.clear();
		pipeline.soSemanticNames.clear();
		pipeline.soStrides.clear();

		pipeline.inputElements.reserve(metadata.inputElements.size());
		pipeline.inputElementSemanticNames.reserve(metadata.inputElements.size());

		for (const ShaderReplacement::ShaderInputElementDisk& diskElement : metadata.inputElements)
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

		pipeline.soDeclarations.reserve(metadata.streamOutputDeclarations.size());
		pipeline.soSemanticNames.reserve(metadata.streamOutputDeclarations.size());

		for (const ShaderReplacement::ShaderStreamOutputDeclarationDisk& diskEntry : metadata.streamOutputDeclarations)
		{
			pipeline.soSemanticNames.push_back(diskEntry.semanticName);

			D3D12_SO_DECLARATION_ENTRY entry{};
			entry.SemanticName = pipeline.soSemanticNames.back().c_str();
			entry.SemanticIndex = diskEntry.semanticIndex;
			entry.StartComponent = (BYTE)diskEntry.startComponent;
			entry.ComponentCount = (BYTE)diskEntry.componentCount;
			entry.OutputSlot = (BYTE)diskEntry.outputSlot;
			pipeline.soDeclarations.push_back(entry);
		}

		for (uint32_t stride : metadata.streamOutputStrides)
			pipeline.soStrides.push_back((UINT)stride);
	}

	void RebindPipelineStateInfoPointerFields(PipelineStateInfo& info)
	{
		if (info.inputElementSemanticNames.size() == info.inputElements.size())
		{
			for (size_t i = 0; i < info.inputElements.size(); ++i)
				info.inputElements[i].SemanticName = info.inputElementSemanticNames[i].c_str();
		}

		if (info.soSemanticNames.size() == info.soDeclarations.size())
		{
			for (size_t i = 0; i < info.soDeclarations.size(); ++i)
				info.soDeclarations[i].SemanticName = info.soSemanticNames[i].c_str();
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

			if (typeIdx >= ARRAYSIZE(kSubobjectSizes))
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("HookD3D12PipelineUtils->ParsePipelineStream: unknown subobject type " + std::to_string(typeIdx) + ", cannot continue");
				return;
			}

			size_t subobjectSize = kSubobjectSizes[typeIdx];

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
					info.rootSignature = subobj->payload;
					break;
				}

				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
				{
					auto* subobj = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, D3D12_SHADER_BYTECODE>*>(ptr);
					if (subobj->payload.pShaderBytecode && subobj->payload.BytecodeLength)
					{
						info.vsHash = Hash::HashMemory(subobj->payload.pShaderBytecode, subobj->payload.BytecodeLength);
						info.vsSize = subobj->payload.BytecodeLength;
						info.vsBytecode.assign((const uint8_t*)subobj->payload.pShaderBytecode, (const uint8_t*)subobj->payload.pShaderBytecode + subobj->payload.BytecodeLength);
					}
					info.isGraphics = true;
					break;
				}

				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
				{
					auto* subobj = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE>*>(ptr);
					if (subobj->payload.pShaderBytecode && subobj->payload.BytecodeLength)
					{
						info.psHash = Hash::HashMemory(subobj->payload.pShaderBytecode, subobj->payload.BytecodeLength);
						info.psSize = subobj->payload.BytecodeLength;
						info.psBytecode.assign((const uint8_t*)subobj->payload.pShaderBytecode, (const uint8_t*)subobj->payload.pShaderBytecode + subobj->payload.BytecodeLength);
					}
					info.isGraphics = true;
					break;
				}

				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
				{
					auto* subobj = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS, D3D12_SHADER_BYTECODE>*>(ptr);
					if (subobj->payload.pShaderBytecode && subobj->payload.BytecodeLength)
					{
						info.gsHash = Hash::HashMemory(subobj->payload.pShaderBytecode, subobj->payload.BytecodeLength);
						info.gsSize = subobj->payload.BytecodeLength;
						info.gsBytecode.assign((const uint8_t*)subobj->payload.pShaderBytecode, (const uint8_t*)subobj->payload.pShaderBytecode + subobj->payload.BytecodeLength);
					}
					info.isGraphics = true;
					break;
				}

				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
				{
					auto* subobj = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS, D3D12_SHADER_BYTECODE>*>(ptr);
					if (subobj->payload.pShaderBytecode && subobj->payload.BytecodeLength)
					{
						info.hsHash = Hash::HashMemory(subobj->payload.pShaderBytecode, subobj->payload.BytecodeLength);
						info.hsSize = subobj->payload.BytecodeLength;
						info.hsBytecode.assign((const uint8_t*)subobj->payload.pShaderBytecode, (const uint8_t*)subobj->payload.pShaderBytecode + subobj->payload.BytecodeLength);
					}
					info.isGraphics = true;
					break;
				}

				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
				{
					auto* subobj = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS, D3D12_SHADER_BYTECODE>*>(ptr);
					if (subobj->payload.pShaderBytecode && subobj->payload.BytecodeLength)
					{
						info.dsHash = Hash::HashMemory(subobj->payload.pShaderBytecode, subobj->payload.BytecodeLength);
						info.dsSize = subobj->payload.BytecodeLength;
						info.dsBytecode.assign((const uint8_t*)subobj->payload.pShaderBytecode, (const uint8_t*)subobj->payload.pShaderBytecode + subobj->payload.BytecodeLength);
					}
					info.isGraphics = true;
					break;
				}

				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
				{
					auto* subobj = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS, D3D12_SHADER_BYTECODE>*>(ptr);
					if (subobj->payload.pShaderBytecode && subobj->payload.BytecodeLength)
					{
						info.csHash = Hash::HashMemory(subobj->payload.pShaderBytecode, subobj->payload.BytecodeLength);
						info.csSize = subobj->payload.BytecodeLength;
						info.csBytecode.assign((const uint8_t*)subobj->payload.pShaderBytecode, (const uint8_t*)subobj->payload.pShaderBytecode + subobj->payload.BytecodeLength);
					}
					info.isCompute = true;
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT:
				{
					auto* subobj = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT, D3D12_INPUT_LAYOUT_DESC>*> (ptr);
					if (subobj->payload.pInputElementDescs && subobj->payload.NumElements > 0)
					{
						info.inputElements.assign(subobj->payload.pInputElementDescs, subobj->payload.pInputElementDescs + subobj->payload.NumElements);
						info.inputElementSemanticNames.clear();
						info.inputElementSemanticNames.reserve(info.inputElements.size());
						for (const D3D12_INPUT_ELEMENT_DESC& element : info.inputElements)
							info.inputElementSemanticNames.push_back(element.SemanticName ? element.SemanticName : "");
						for (size_t i = 0; i < info.inputElements.size(); ++i)
							info.inputElements[i].SemanticName = info.inputElementSemanticNames[i].c_str();
					}
					break;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT:
				{
					auto* subobj = reinterpret_cast<const PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT, D3D12_STREAM_OUTPUT_DESC>* > (ptr);
					if (subobj->payload.pSODeclaration && subobj->payload.NumEntries > 0)
					{
						info.soDeclarations.assign(subobj->payload.pSODeclaration, subobj->payload.pSODeclaration + subobj->payload.NumEntries);
						info.soSemanticNames.clear();
						info.soSemanticNames.reserve(info.soDeclarations.size());
						for (const D3D12_SO_DECLARATION_ENTRY& entry : info.soDeclarations)
							info.soSemanticNames.push_back(entry.SemanticName ? entry.SemanticName : "");
						for (size_t i = 0; i < info.soDeclarations.size(); ++i)
							info.soDeclarations[i].SemanticName = info.soSemanticNames[i].c_str();
					}
					if (subobj->payload.pBufferStrides && subobj->payload.NumStrides > 0)
					{
						info.soStrides.assign(subobj->payload.pBufferStrides, subobj->payload.pBufferStrides + subobj->payload.NumStrides);
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

		pipelineInfo.swapChainBuffers = desc.BufferCount;
		pipelineInfo.swapChainFormat = desc.BufferDesc.Format;

		ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12PipelineUtils->GatherD3D12PipelineInfo: swapChainBuffers: " + std::to_string(pipelineInfo.swapChainBuffers));
		ShaderInjectorGUI::WriteToRuntimeLog("HookD3D12PipelineUtils->GatherD3D12PipelineInfo: swapChainFormat: " + std::to_string(pipelineInfo.swapChainFormat));

		IDXGIDevice* dxgiDevice = nullptr;

		if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))))
		{
			IDXGIAdapter* adapter = nullptr;

			if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter)))
			{
				DXGI_ADAPTER_DESC adapterDesc{};
				adapter->GetDesc(&adapterDesc);

				char gpuName[256]{};
				wcstombs_s(nullptr, gpuName, adapterDesc.Description, sizeof(gpuName));

				pipelineInfo.gpuName = gpuName;
				pipelineInfo.vendorId = adapterDesc.VendorId;
				pipelineInfo.deviceId = adapterDesc.DeviceId;
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
				pipelineInfo.gpuName = "CreateDXGIFactory1 failed";
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

				char gpuName[256];
				wcstombs_s(nullptr, gpuName, sizeof(gpuName), desc.Description, _TRUNCATE);

				pipelineInfo.gpuName = gpuName;
				pipelineInfo.vendorId = desc.VendorId;
				pipelineInfo.deviceId = desc.DeviceId;
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
}
