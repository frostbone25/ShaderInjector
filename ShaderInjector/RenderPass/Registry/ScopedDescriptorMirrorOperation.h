#pragma once

namespace RenderPassResourceRegistry
{
	//keep the mirror's own D3D12 calls from being recorded as new game descriptors.
	class ScopedDescriptorMirrorOperation
	{
		bool previousState = false;

	public:
		ScopedDescriptorMirrorOperation();
		~ScopedDescriptorMirrorOperation();

		ScopedDescriptorMirrorOperation(const ScopedDescriptorMirrorOperation&) = delete;
		ScopedDescriptorMirrorOperation& operator=(const ScopedDescriptorMirrorOperation&) = delete;
	};
}
