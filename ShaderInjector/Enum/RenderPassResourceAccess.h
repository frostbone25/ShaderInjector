#pragma once

#include "JsonHelper.h"

namespace RenderPass
{
	enum class ResourceAccess
	{
		ShaderResource,
		UnorderedAccess,
		RenderTarget,
		DepthStencil,
		CopySource,
		CopyDestination,
	};

	NLOHMANN_JSON_SERIALIZE_ENUM(ResourceAccess,
								 {
									 {ResourceAccess::ShaderResource, "ShaderResource"},
									 {ResourceAccess::UnorderedAccess, "UnorderedAccess"},
									 {ResourceAccess::RenderTarget, "RenderTarget"},
									 {ResourceAccess::DepthStencil, "DepthStencil"},
									 {ResourceAccess::CopySource, "CopySource"},
									 {ResourceAccess::CopyDestination, "CopyDestination"},
								 })
} //namespace RenderPass
