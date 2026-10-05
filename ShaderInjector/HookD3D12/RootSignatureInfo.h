#pragma once

#include <cstdint>
#include <vector>
#include <d3d12.h>
#include <wrl/client.h>

namespace HookD3D12
{
	struct RootSignatureInfo
	{
		uint64_t rootSignatureHash = 0;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> retainedRootSignature;
		std::vector<uint8_t> rootSignatureBlob;
	};
} //namespace HookD3D12
