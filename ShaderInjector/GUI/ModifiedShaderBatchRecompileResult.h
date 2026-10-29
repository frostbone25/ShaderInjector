#pragma once

namespace ShaderInjectorGUI
{
	//show how many selected packages were queued and how many linked targets wait for their blob.
	struct ModifiedShaderBatchRecompileResult
	{
		int selectedPackageCount = 0;
		int skippedInactivePackageCount = 0;
		int queuedPackageCount = 0;
		int failedPackageCount = 0;
		int linkedShaderTargetCount = 0;
		int pendingReloadShaderTargetCount = 0;
		int skippedInactiveShaderTargetCount = 0;
	};
} //namespace ShaderInjectorGUI
