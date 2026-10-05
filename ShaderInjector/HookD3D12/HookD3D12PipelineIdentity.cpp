#include <cstring>
#include <type_traits>
#include <vector>

#include "Hash/Hash.h"
#include "HookD3D12.h"

namespace HookD3D12
{
	namespace
	{
		template <typename ValueType>
		void AppendValue(std::vector<uint8_t>& canonicalBytes, const ValueType& value)
		{
			static_assert(std::is_trivially_copyable<ValueType>::value, "canonical values must be trivially copyable");
			const uint8_t* valueBytes = reinterpret_cast<const uint8_t*>(&value);
			canonicalBytes.insert(canonicalBytes.end(), valueBytes, valueBytes + sizeof(ValueType));
		}

		template <typename PayloadType>
		PayloadType ReadPayload(const uint8_t* subobjectBytes)
		{
			PayloadType payload{};
			std::memcpy(&payload, subobjectBytes + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE), sizeof(PayloadType));
			return payload;
		}

		void AppendBlendOperation(std::vector<uint8_t>& canonicalBytes, const D3D12_RENDER_TARGET_BLEND_DESC& blendState)
		{
			AppendValue(canonicalBytes, blendState.BlendEnable);
			AppendValue(canonicalBytes, blendState.LogicOpEnable);
			AppendValue(canonicalBytes, blendState.SrcBlend);
			AppendValue(canonicalBytes, blendState.DestBlend);
			AppendValue(canonicalBytes, blendState.BlendOp);
			AppendValue(canonicalBytes, blendState.SrcBlendAlpha);
			AppendValue(canonicalBytes, blendState.DestBlendAlpha);
			AppendValue(canonicalBytes, blendState.BlendOpAlpha);
			AppendValue(canonicalBytes, blendState.LogicOp);
			AppendValue(canonicalBytes, blendState.RenderTargetWriteMask);
		}

		void AppendStencilOperation(std::vector<uint8_t>& canonicalBytes, const D3D12_DEPTH_STENCILOP_DESC& stencilState)
		{
			AppendValue(canonicalBytes, stencilState.StencilFailOp);
			AppendValue(canonicalBytes, stencilState.StencilDepthFailOp);
			AppendValue(canonicalBytes, stencilState.StencilPassOp);
			AppendValue(canonicalBytes, stencilState.StencilFunc);
		}

		void AppendDepthStencilState(std::vector<uint8_t>& canonicalBytes, const D3D12_DEPTH_STENCIL_DESC& depthStencilState)
		{
			AppendValue(canonicalBytes, depthStencilState.DepthEnable);
			AppendValue(canonicalBytes, depthStencilState.DepthWriteMask);
			AppendValue(canonicalBytes, depthStencilState.DepthFunc);
			AppendValue(canonicalBytes, depthStencilState.StencilEnable);
			AppendValue(canonicalBytes, depthStencilState.StencilReadMask);
			AppendValue(canonicalBytes, depthStencilState.StencilWriteMask);
			AppendStencilOperation(canonicalBytes, depthStencilState.FrontFace);
			AppendStencilOperation(canonicalBytes, depthStencilState.BackFace);
		}

		void AppendDepthStencilState(std::vector<uint8_t>& canonicalBytes, const D3D12_DEPTH_STENCIL_DESC1& depthStencilState)
		{
			AppendValue(canonicalBytes, depthStencilState.DepthEnable);
			AppendValue(canonicalBytes, depthStencilState.DepthWriteMask);
			AppendValue(canonicalBytes, depthStencilState.DepthFunc);
			AppendValue(canonicalBytes, depthStencilState.StencilEnable);
			AppendValue(canonicalBytes, depthStencilState.StencilReadMask);
			AppendValue(canonicalBytes, depthStencilState.StencilWriteMask);
			AppendStencilOperation(canonicalBytes, depthStencilState.FrontFace);
			AppendStencilOperation(canonicalBytes, depthStencilState.BackFace);
			AppendValue(canonicalBytes, depthStencilState.DepthBoundsTestEnable);
		}

		bool AppendFixedState(std::vector<uint8_t>& canonicalBytes, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type, const uint8_t* subobjectBytes)
		{
			AppendValue(canonicalBytes, type);

			switch (type)
			{
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND:
				{
					const D3D12_BLEND_DESC payload = ReadPayload<D3D12_BLEND_DESC>(subobjectBytes);
					AppendValue(canonicalBytes, payload.AlphaToCoverageEnable);
					AppendValue(canonicalBytes, payload.IndependentBlendEnable);

					//without independent blending, D3D12 uses only the first slot for every render target.
					UINT blendTargetCount = 1;

					if (payload.IndependentBlendEnable)
						blendTargetCount = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;

					for (UINT targetIndex = 0; targetIndex < blendTargetCount; ++targetIndex)
						AppendBlendOperation(canonicalBytes, payload.RenderTarget[targetIndex]);

					return true;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:
					AppendValue(canonicalBytes, ReadPayload<UINT>(subobjectBytes));
					return true;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER:
				{
					const D3D12_RASTERIZER_DESC payload = ReadPayload<D3D12_RASTERIZER_DESC>(subobjectBytes);
					AppendValue(canonicalBytes, payload.FillMode);
					AppendValue(canonicalBytes, payload.CullMode);
					AppendValue(canonicalBytes, payload.FrontCounterClockwise);
					AppendValue(canonicalBytes, payload.DepthBias);
					AppendValue(canonicalBytes, payload.DepthBiasClamp);
					AppendValue(canonicalBytes, payload.SlopeScaledDepthBias);
					AppendValue(canonicalBytes, payload.DepthClipEnable);
					AppendValue(canonicalBytes, payload.MultisampleEnable);
					AppendValue(canonicalBytes, payload.AntialiasedLineEnable);
					AppendValue(canonicalBytes, payload.ForcedSampleCount);
					AppendValue(canonicalBytes, payload.ConservativeRaster);
					return true;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL:
					AppendDepthStencilState(canonicalBytes, ReadPayload<D3D12_DEPTH_STENCIL_DESC>(subobjectBytes));
					return true;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE:
					AppendValue(canonicalBytes, ReadPayload<D3D12_INDEX_BUFFER_STRIP_CUT_VALUE>(subobjectBytes));
					return true;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:
					AppendValue(canonicalBytes, ReadPayload<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(subobjectBytes));
					return true;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS:
				{
					const D3D12_RT_FORMAT_ARRAY payload = ReadPayload<D3D12_RT_FORMAT_ARRAY>(subobjectBytes);

					if (payload.NumRenderTargets > D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT)
						return false;

					AppendValue(canonicalBytes, payload.NumRenderTargets);

					//inactive slots may contain arbitrary bytes and have no effect on the actual PSO.
					for (UINT targetIndex = 0; targetIndex < payload.NumRenderTargets; ++targetIndex)
						AppendValue(canonicalBytes, payload.RTFormats[targetIndex]);

					return true;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT:
					AppendValue(canonicalBytes, ReadPayload<DXGI_FORMAT>(subobjectBytes));
					return true;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC:
				{
					const DXGI_SAMPLE_DESC payload = ReadPayload<DXGI_SAMPLE_DESC>(subobjectBytes);
					AppendValue(canonicalBytes, payload.Count);
					AppendValue(canonicalBytes, payload.Quality);
					return true;
				}
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK:
					AppendValue(canonicalBytes, ReadPayload<UINT>(subobjectBytes));
					return true;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS:
					AppendValue(canonicalBytes, ReadPayload<D3D12_PIPELINE_STATE_FLAGS>(subobjectBytes));
					return true;
				case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1:
					AppendDepthStencilState(canonicalBytes, ReadPayload<D3D12_DEPTH_STENCIL_DESC1>(subobjectBytes));
					return true;
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
					return true;
				default:
					return false;
			}
		}
	}

	uint64_t CanonicalPipelineFixedFunctionStateHash(const std::vector<uint8_t>& streamBlob)
	{
		if (streamBlob.empty())
			return 0;

		std::vector<uint8_t> canonicalBytes;
		const uint8_t* streamPosition = streamBlob.data();
		const uint8_t* streamEnd = streamPosition + streamBlob.size();

		while (streamPosition < streamEnd)
		{
			if (static_cast<size_t>(streamEnd - streamPosition) < sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE))
				return 0;

			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type{};
			std::memcpy(&type, streamPosition, sizeof(type));
			const UINT typeIndex = static_cast<UINT>(type);

			if (typeIndex >= ARRAYSIZE(subobjectSizes) || subobjectSizes[typeIndex] == 0)
				return 0;

			const size_t subobjectSize = subobjectSizes[typeIndex];
			if (static_cast<size_t>(streamEnd - streamPosition) < subobjectSize)
				return 0;

			if (!AppendFixedState(canonicalBytes, type, streamPosition))
				return 0;

			streamPosition += subobjectSize;
		}

		return Hash::HashMemory(canonicalBytes.data(), canonicalBytes.size());
	}
}
