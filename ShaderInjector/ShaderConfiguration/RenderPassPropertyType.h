#pragma once

namespace ShaderConfiguration
{
	enum class RenderPassPropertyType
	{
		Enabled = 0,
		DownscalePowerOfTwo = 1,
		EnabledMultiple = 2,
		DownscalePowerOfTwoMultiple = 3
	};

	inline bool IsEnabledRenderPassProperty(RenderPassPropertyType propertyType)
	{
		return propertyType == RenderPassPropertyType::Enabled || propertyType == RenderPassPropertyType::EnabledMultiple;
	}

	inline bool IsDownscaleRenderPassProperty(RenderPassPropertyType propertyType)
	{
		return propertyType == RenderPassPropertyType::DownscalePowerOfTwo || propertyType == RenderPassPropertyType::DownscalePowerOfTwoMultiple;
	}

	inline bool IsMultipleRenderPassProperty(RenderPassPropertyType propertyType)
	{
		return propertyType == RenderPassPropertyType::EnabledMultiple || propertyType == RenderPassPropertyType::DownscalePowerOfTwoMultiple;
	}
} //namespace ShaderConfiguration
