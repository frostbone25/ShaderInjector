#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "RenderPass/RenderPass.h"

namespace RenderPassResourceRegistry
{
	using DescriptorRecord = std::shared_ptr<const RenderPass::ResourceBindingDiagnostic>;
	constexpr size_t descriptorPageSize = 512;

	//store descriptor metadata in fixed pages so unrelated threads touch separate memory.
	struct DescriptorPage
	{
		std::array<DescriptorRecord, descriptorPageSize> descriptors;
		std::array<std::atomic<const RenderPass::ResourceBindingDiagnostic*>, descriptorPageSize> descriptorIdentities{};
		std::atomic<uint32_t> trackedDescriptorCount{0};
	};
} //namespace RenderPassResourceRegistry
