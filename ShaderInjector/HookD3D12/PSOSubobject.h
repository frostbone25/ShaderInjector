#pragma once

#include <d3d12.h>

namespace HookD3D12
{
	//match the aligned records used by D3D12 pipeline state streams.
	template <D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type, typename PayloadType>
	struct alignas(void*) PSOSubobject
	{
		D3D12_PIPELINE_STATE_SUBOBJECT_TYPE subobjectType;
		PayloadType payloadData;
	};
} //namespace HookD3D12
