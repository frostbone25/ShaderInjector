#pragma once

namespace ShaderInjectorGUI
{
	//summarize the active targets reloaded after a user requests a refresh.
	struct ShaderTargetReloadResult
	{
		int activeShaderTargetCount = 0;
		int reloadedShaderTargetCount = 0;
		int skippedInactiveShaderTargetCount = 0;
	};
} //namespace ShaderInjectorGUI
