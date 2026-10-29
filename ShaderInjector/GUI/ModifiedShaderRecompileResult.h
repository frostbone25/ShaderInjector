#pragma once

namespace ShaderInjectorGUI
{
	//report how much of a modified shader's linked target set was queued for a background refresh.
	struct ModifiedShaderRecompileResult
	{
		bool compileQueued = false;
		int linkedShaderTargetCount = 0;
		int pendingReloadShaderTargetCount = 0;
		int skippedInactiveShaderTargetCount = 0;
	};
} //namespace ShaderInjectorGUI
