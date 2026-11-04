#pragma once

#include <string>
#include <vector>

#include "RenderPass/RenderPassDisk.h"
#include "ShaderConfiguration/DocumentDisk.h"

namespace RenderPass
{
	bool ApplyConfigurationProperties(
		const ShaderConfiguration::DocumentDisk& document,
		std::vector<RenderPassDisk>& effectivePasses,
		std::string& error);

	bool ValidateConfigurationProperties(
		const ShaderConfiguration::DocumentDisk& document,
		const std::vector<RenderPassDisk>& passes,
		std::string& error);
} //namespace RenderPass
