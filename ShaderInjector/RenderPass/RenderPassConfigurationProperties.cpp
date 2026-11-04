#include "RenderPass/RenderPassConfigurationProperties.h"

#include <algorithm>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Enum/ShaderResourceResolutionMode.h"
#include "ShaderConfiguration/PropertyType.h"
#include "ShaderConfiguration/RenderPassPropertyType.h"
#include "ShaderConfiguration/ShaderConfiguration.h"
#include "ShaderResource/ShaderResource.h"

namespace RenderPass
{
	namespace
	{
		std::vector<std::string> GetTargetIDs(const ShaderConfiguration::PropertyDisk& property)
		{
			if (!property.renderPassIDs.empty())
				return property.renderPassIDs;

			if (!property.renderPassID.empty())
				return {property.renderPassID};

			return {};
		}

		int GetSettingKey(ShaderConfiguration::RenderPassPropertyType propertyType)
		{
			if (ShaderConfiguration::IsEnabledRenderPassProperty(propertyType))
				return 0;

			return 1;
		}
	}

	bool ApplyConfigurationProperties(
		const ShaderConfiguration::DocumentDisk& document,
		std::vector<RenderPassDisk>& effectivePasses,
		std::string& error)
	{
		std::vector<RenderPassDisk> candidatePasses = effectivePasses;
		std::set<std::pair<std::string, int>> bindings;

		for (const ShaderConfiguration::PropertyDisk& property : document.properties)
		{
			if (property.propertyType != ShaderConfiguration::PropertyType::RenderPass)
				continue;

			const bool enabledProperty = ShaderConfiguration::IsEnabledRenderPassProperty(property.renderPassPropertyType);
			if (!enabledProperty && !ShaderConfiguration::IsDownscaleRenderPassProperty(property.renderPassPropertyType))
			{
				error = "A render pass property has an unsupported binding type.";
				return false;
			}

			const std::vector<std::string> targetIDs = GetTargetIDs(property);
			if (targetIDs.empty())
			{
				error = "A render pass property is missing its target render pass ID.";
				return false;
			}
			if (ShaderConfiguration::IsMultipleRenderPassProperty(property.renderPassPropertyType) != (targetIDs.size() > 1))
			{
				error = "A render pass property has a target count that does not match its binding type.";
				return false;
			}

			std::set<std::string> uniqueTargetIDs;
			std::vector<RenderPassDisk*> targets;
			for (const std::string& targetID : targetIDs)
			{
				if (targetID.empty() || !uniqueTargetIDs.insert(targetID).second)
				{
					error = "Render-pass targets must be non-empty and unique.";
					return false;
				}

				auto pass = std::find_if(candidatePasses.begin(), candidatePasses.end(), [&](const RenderPassDisk& candidate)
					{ return candidate.id == targetID; });
				if (pass == candidatePasses.end())
				{
					error = "A render pass property targets an unknown render pass: " + targetID;
					return false;
				}
				if (std::find_if(std::next(pass), candidatePasses.end(), [&](const RenderPassDisk& candidate)
					{ return candidate.id == targetID; }) != candidatePasses.end())
				{
					error = "A render pass property targets an ambiguous duplicate render pass ID: " + targetID;
					return false;
				}

				if (!bindings.emplace(targetID, GetSettingKey(property.renderPassPropertyType)).second)
				{
					error = "More than one configuration property targets the same render pass setting: " + targetID;
					return false;
				}

				targets.push_back(&*pass);
			}

			if (enabledProperty)
			{
				if (property.valueType != "bool")
				{
					error = "The enabled render pass property must have boolean value type.";
					return false;
				}

				bool enabled = false;
				if (!ShaderConfiguration::TryGetBoolean(property, enabled))
				{
					error = "The enabled render pass property has an invalid boolean value.";
					return false;
				}

				for (RenderPassDisk* pass : targets)
					pass->enabled = enabled;

				continue;
			}

			if (property.valueType != "int")
			{
				error = "The downscale render pass property must have integer value type.";
				return false;
			}

			int factor = 0;
			if (!ShaderConfiguration::TryGetInteger(property, factor) || factor < 1 || factor > 1024 || (factor & (factor - 1)) != 0)
			{
				error = "The downscale render pass property must be a power of two from 1 through 1024.";
				return false;
			}

			for (RenderPassDisk* pass : targets)
			{
				if (pass->type == RenderPassType::DownsampleUpsample)
				{
					if (factor < 2)
					{
						error = "The downsample upsample chain factor must be at least 2.";
						return false;
					}

					pass->chainDownsampleFactor = static_cast<uint32_t>(factor);
					continue;
				}

				//ordinary resolution scaling retains its smaller allocation limit.
				if (!ShaderResource::IsValidDownscaleFactor(static_cast<uint32_t>(factor)))
				{
					error = "Ordinary render pass downscale factors cannot exceed 16; chain factors can reach 1024.";
					return false;
				}

				pass->resolution.mode = ShaderResource::ResolutionMode::DownscalePowerOfTwo;
				pass->resolution.downscaleFactor = static_cast<uint32_t>(factor);
				for (RuntimeResourceDefinitionDisk& resource : pass->runtimeResources)
				{
					resource.texture.resolution.mode = ShaderResource::ResolutionMode::DownscalePowerOfTwo;
					resource.texture.resolution.downscaleFactor = static_cast<uint32_t>(factor);
				}
			}
		}

		effectivePasses = std::move(candidatePasses);
		error.clear();
		return true;
	}

	bool ValidateConfigurationProperties(
		const ShaderConfiguration::DocumentDisk& document,
		const std::vector<RenderPassDisk>& passes,
		std::string& error)
	{
		std::vector<RenderPassDisk> candidatePasses = passes;
		return ApplyConfigurationProperties(document, candidatePasses, error);
	}
} //namespace RenderPass
