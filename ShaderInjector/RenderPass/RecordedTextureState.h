#pragma once

namespace RenderPassMipChain
{
	struct MipTextureResources;

	//keep the resource state recorded in one execution slot until its command list retires.
	struct RecordedTextureState
	{
		MipTextureResources* resources = nullptr;
		bool allSubresourcesShaderReadable = false;
	};
} //namespace RenderPassMipChain
