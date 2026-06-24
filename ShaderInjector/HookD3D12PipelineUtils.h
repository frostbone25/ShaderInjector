#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>

#include "ShaderReplacement.h"

namespace HookD3D12
{
	struct GraphicsPipelineInfo;
	struct PipelineStateInfo;
	struct D3D12PipelineInfo;

	template<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type, typename PayloadT>
	struct alignas(void*) PSOSubobject
	{
		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type;
		PayloadT payload;
	};

	static const size_t kSubobjectSizes[] =
	{
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,        ID3D12RootSignature*>),
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS,                    D3D12_SHADER_BYTECODE>),
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS,                    D3D12_SHADER_BYTECODE>),
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS,                    D3D12_SHADER_BYTECODE>),
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS,                    D3D12_SHADER_BYTECODE>),
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS,                    D3D12_SHADER_BYTECODE>),
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS,                    D3D12_SHADER_BYTECODE>),
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT,         D3D12_STREAM_OUTPUT_DESC>),
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND,                 D3D12_BLEND_DESC>),
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK,           UINT>),
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER,            D3D12_RASTERIZER_DESC>),
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL,         D3D12_DEPTH_STENCIL_DESC>),
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT,          D3D12_INPUT_LAYOUT_DESC>),
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE,    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE>),
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY,    D3D12_PRIMITIVE_TOPOLOGY_TYPE>),
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY>),
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT,  DXGI_FORMAT>),
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC,           DXGI_SAMPLE_DESC>),
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK,             UINT>),
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO,            D3D12_CACHED_PIPELINE_STATE>),
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS,                 D3D12_PIPELINE_STATE_FLAGS>),
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1,        D3D12_DEPTH_STENCIL_DESC1>),
		sizeof(PSOSubobject<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING,       D3D12_VIEW_INSTANCING_DESC>),
	};

	std::string PointerToString(const void* ptr);

	void FillCommonReplacementHashes(ShaderReplacement::ShaderReplacementDisk& replacement, uint64_t vsHash, uint64_t psHash, uint64_t csHash, uint64_t gsHash, uint64_t hsHash, uint64_t dsHash);
	void FillCommonReplacementStageLengths(ShaderReplacement::ShaderReplacementDisk& replacement, SIZE_T vsSize, SIZE_T psSize, SIZE_T csSize, SIZE_T gsSize, SIZE_T hsSize, SIZE_T dsSize);

	std::string HashStructText(const void* data, size_t size);
	std::string JoinUIntValues(const UINT* values, UINT count);
	std::string RenderTargetFormatsSignature(const DXGI_FORMAT* formats, UINT count);
	std::string InputLayoutSignature(const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputElements);
	std::string StreamOutputSignature(const std::vector<D3D12_SO_DECLARATION_ENTRY>& declarations, const std::vector<UINT>& strides);
	std::string PipelineStreamSubobjectTypeSignature(const std::vector<uint8_t>& streamBlob);

	void FillInputAndStreamOutputSignatures(ShaderReplacement::ShaderReplacementDisk& replacement, const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputElements, const std::vector<D3D12_SO_DECLARATION_ENTRY>& soDeclarations, const std::vector<UINT>& soStrides);
	void FillGraphicsReplacementPortableState(ShaderReplacement::ShaderReplacementDisk& replacement, const GraphicsPipelineInfo& pipeline);
	void FillStreamReplacementPortableStateFromBlob(ShaderReplacement::ShaderReplacementDisk& replacement, const PipelineStateInfo& pipeline);

	ShaderReplacement::ShaderPipelineStreamMetadataDisk BuildPipelineStreamMetadata(const PipelineStateInfo& pipeline);

	void ApplyPipelineStreamMetadata(const ShaderReplacement::ShaderPipelineStreamMetadataDisk& metadata, PipelineStateInfo& pipeline);
	void RebindPipelineStateInfoPointerFields(PipelineStateInfo& info);
	void ParsePipelineStream(const D3D12_PIPELINE_STATE_STREAM_DESC* desc, PipelineStateInfo& info);
	void GatherD3D12PipelineInfo(IDXGISwapChain3* swapChain, ID3D12Device* device, ID3D12CommandQueue* commandQueue, D3D12PipelineInfo& pipelineInfo);
}
