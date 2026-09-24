#pragma once

#include <vector>

#include "RenderPass/Registry/RootParameterLayout.h"

namespace RenderPassResourceRegistry
{
	//retain a parsed root signature so descriptor lookups avoid reparsing bytecode.
	struct RootSignatureLayout
	{
		std::vector<RootParameterLayout> parameters;
	};
} //namespace RenderPassResourceRegistry
