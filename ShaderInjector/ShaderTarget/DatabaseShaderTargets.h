#pragma once

//custom
#include "HookD3D12.h"

namespace HookD3D12
{
	bool CompileShaderTarget(int replacementIndex);
	bool ReloadShaderTarget(int replacementIndex);
	bool SaveShaderTarget(int replacementIndex);
	bool DeleteShaderTarget(int replacementIndex);
	bool IsShaderTargetEffectivelyEnabled(const ShaderTarget::ShaderTargetDisk& replacement);
	void RefreshShaderTargetsForModifiedShaderStateChange();

	void RefreshLoadedShaderTargets();

	void SyncShaderTargetNameBuffer();
}
