#pragma once

namespace ShaderInjectorGUI
{
	//report how much of a modified shader's linked target set was refreshed.
	struct ModifiedShaderRecompileResult
	{
		bool compiled = false;
		int linkedShaderTargetCount = 0;
		int reloadedShaderTargetCount = 0;
		int skippedInactiveShaderTargetCount = 0;
	};
} //namespace ShaderInjectorGUI
