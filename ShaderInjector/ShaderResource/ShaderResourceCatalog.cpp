#include "ShaderResource/ShaderResourceCatalog.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace ShaderResourceCatalog
{
	namespace
	{
		std::mutex gCatalogMutex;
		std::unordered_map<std::string, ShaderResource::CatalogEntry> gCatalog;

		std::string CatalogKey(ShaderResource::ResourceOrigin origin, const std::string& resourceId)
		{
			return std::to_string(static_cast<unsigned int>(origin)) + ":" + resourceId;
		}
	}

	void PublishDiskResources(const std::vector<ShaderResource::TextureDisk>& resources)
	{
		std::lock_guard<std::mutex> lock(gCatalogMutex);
		for (auto resourceIt = gCatalog.begin(); resourceIt != gCatalog.end();)
		{
			if (resourceIt->second.origin == ShaderResource::ResourceOrigin::Disk)
				resourceIt = gCatalog.erase(resourceIt);
			else
				++resourceIt;
		}

		for (const ShaderResource::TextureDisk& resource : resources)
		{
			ShaderResource::CatalogEntry entry{};
			entry.id = resource.id;
			entry.name = resource.name;
			entry.origin = ShaderResource::ResourceOrigin::Disk;
			entry.lifetime = ShaderResource::ResourceLifetime::Immutable;
			entry.dimension = resource.dimension;
			entry.width = resource.width;
			entry.height = resource.height;
			entry.depth = resource.depth;
			entry.arraySize = resource.arraySize;
			entry.mipLevels = resource.mipLevels;
			entry.format = resource.format;
			entry.status = resource.validationError.empty() ? "Available" : resource.validationError;
			gCatalog[CatalogKey(entry.origin, entry.id)] = std::move(entry);
		}
	}

	void Upsert(const ShaderResource::CatalogEntry& entry)
	{
		if (entry.id.empty())
			return;
		std::lock_guard<std::mutex> lock(gCatalogMutex);
		gCatalog[CatalogKey(entry.origin, entry.id)] = entry;
	}

	void SetResident(
		ShaderResource::ResourceOrigin origin,
		const std::string& resourceId,
		bool resident,
		const std::string& status)
	{
		std::lock_guard<std::mutex> lock(gCatalogMutex);
		const auto resourceIt = gCatalog.find(CatalogKey(origin, resourceId));
		if (resourceIt == gCatalog.end())
			return;
		resourceIt->second.resident = resident;
		if (!status.empty())
			resourceIt->second.status = status;
	}

	void RemoveRuntimeResourcesByOwner(const std::string& ownerRenderPassId)
	{
		std::lock_guard<std::mutex> lock(gCatalogMutex);
		for (auto resourceIt = gCatalog.begin(); resourceIt != gCatalog.end();)
		{
			if (resourceIt->second.origin == ShaderResource::ResourceOrigin::Runtime &&
				resourceIt->second.ownerRenderPassId == ownerRenderPassId)
			{
				resourceIt = gCatalog.erase(resourceIt);
			}
			else
			{
				++resourceIt;
			}
		}
	}

	void ClearRuntimeResources()
	{
		std::lock_guard<std::mutex> lock(gCatalogMutex);
		for (auto resourceIt = gCatalog.begin(); resourceIt != gCatalog.end();)
		{
			if (resourceIt->second.origin == ShaderResource::ResourceOrigin::Runtime)
				resourceIt = gCatalog.erase(resourceIt);
			else
				++resourceIt;
		}
	}

	std::vector<ShaderResource::CatalogEntry> GetSnapshot()
	{
		std::lock_guard<std::mutex> lock(gCatalogMutex);
		std::vector<ShaderResource::CatalogEntry> snapshot;
		snapshot.reserve(gCatalog.size());
		for (const auto& resource : gCatalog)
			snapshot.push_back(resource.second);
		std::sort(snapshot.begin(), snapshot.end(), [](const auto& left, const auto& right)
		{
			if (left.origin != right.origin)
				return left.origin < right.origin;
			if (left.name != right.name)
				return left.name < right.name;
			return left.id < right.id;
		});
		return snapshot;
	}
}
