#pragma once

#include <string>
#include <vector>

#include "ShaderResource/ShaderResource.h"

namespace ShaderResourceCatalog
{
	void PublishDiskResources(const std::vector<ShaderResource::TextureDisk>& resources);
	void Upsert(const ShaderResource::CatalogEntry& entry);
	void SetResident(
		ShaderResource::ResourceOrigin origin,
		const std::string& resourceId,
		bool resident,
		const std::string& status = {});
	void RemoveRuntimeResourcesByOwner(const std::string& ownerRenderPassId);
	void ClearRuntimeResources();
	std::vector<ShaderResource::CatalogEntry> GetSnapshot();
}
