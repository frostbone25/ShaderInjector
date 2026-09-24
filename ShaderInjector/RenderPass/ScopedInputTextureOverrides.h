#pragma once

#include <cstddef>

#include <d3d12.h>

namespace RenderPassTexturePool
{
	//restore the previous input overrides when one graph execution scope ends.
	class ScopedInputTextureOverrides
	{
		size_t previousCount = 0;
		ID3D12GraphicsCommandList* previousCommandList = nullptr;

	  public:
		explicit ScopedInputTextureOverrides(ID3D12GraphicsCommandList* commandList = nullptr);
		~ScopedInputTextureOverrides();
		ScopedInputTextureOverrides(const ScopedInputTextureOverrides&) = delete;
		ScopedInputTextureOverrides& operator=(const ScopedInputTextureOverrides&) = delete;
	};
} //namespace RenderPassTexturePool
