#pragma once

#include <string>
#include <vector>

#include "ShaderResource/ShaderResource.h"

namespace DatabaseShaderResources
{
	void RefreshShaderResources();
	void EnsureShaderResourcesLoaded();
	const std::vector<ShaderResource::TextureDisk>& GetShaderResources();
	const ShaderResource::TextureDisk* FindShaderResourceById(const std::string& resourceId);
}
