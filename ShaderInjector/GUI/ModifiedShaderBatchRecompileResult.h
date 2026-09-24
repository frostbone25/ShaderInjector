#pragma once

namespace ShaderInjectorGUI
{
	//show how many selected packages and linked targets a batch compile updated.
	struct ModifiedShaderBatchRecompileResult
	{
		int selectedPackageCount = 0;
		int skippedInactivePackageCount = 0;
		int compiledPackageCount = 0;
		int failedPackageCount = 0;
		int linkedShaderTargetCount = 0;
		int reloadedShaderTargetCount = 0;
		int skippedInactiveShaderTargetCount = 0;
	};
} //namespace ShaderInjectorGUI
