#pragma once

namespace RenderPass
{
	//keep the saved format and timing names together so every pass uses the same schema.
	inline constexpr const char* formatName = "ShaderInjector.RenderPass";
	inline constexpr int currentSchemaVersion = 11;
	inline constexpr const char* timingBefore = "Before";
	inline constexpr const char* timingAfter = "After";
} //namespace RenderPass
