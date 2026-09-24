#pragma once

#include <string>

#include "RenderPass/RenderPass.h"

namespace RenderPassMipChain
{
	//report whether one runtime mip pass was attempted and what prevented success.
	struct ExecutionResult
	{
		const RenderPass::RenderPassDisk* renderPass = nullptr;
		bool attempted = false;
		bool succeeded = false;
		std::string error;
	};
} //namespace RenderPassMipChain
