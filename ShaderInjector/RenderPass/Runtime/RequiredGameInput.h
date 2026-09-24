#pragma once

#include "RenderPass/RenderPass.h"

namespace RenderPassRuntime
{
	//identify an input that must be available before the pass can execute.
	struct RequiredGameInput
	{
		const RenderPass::RenderPassDisk* renderPass = nullptr;
		const RenderPass::LogicalResourceBindingDisk* input = nullptr;
	};
} //namespace RenderPassRuntime
