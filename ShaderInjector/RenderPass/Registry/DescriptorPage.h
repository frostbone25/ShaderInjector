#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <shared_mutex>

#include "RenderPass/RenderPass.h"

namespace RenderPassResourceRegistry
{
	using DescriptorRecord = std::shared_ptr<const RenderPass::ResourceBindingDiagnostic>;
	constexpr size_t descriptorPageSize = 512;

	//store descriptor metadata in fixed pages so unrelated threads touch separate memory.
	struct DescriptorPage
	{
		//protect shared owners per page instead of serializing every heap through the STL shared_ptr spinlock.
		mutable std::shared_mutex descriptorMutex;
		//mirror readers and writers synchronize separately from diagnostic metadata updates.
		mutable std::shared_mutex mirrorMutex;
		//late-observed heaps may have metadata without a native mirror write; never substitute those known views with empty descriptors.
		std::array<bool, descriptorPageSize> mirrorDescriptorsWritten{};
		//unknown pre-hook contents stay unsafe even when copied into a freshly captured heap.
		std::array<bool, descriptorPageSize> mirrorDescriptorsUnobserved{};
		std::array<DescriptorRecord, descriptorPageSize> descriptors;
		std::array<std::atomic<const RenderPass::ResourceBindingDiagnostic*>, descriptorPageSize> descriptorIdentities{};
		std::atomic<uint32_t> trackedDescriptorCount{0};
	};
} //namespace RenderPassResourceRegistry
