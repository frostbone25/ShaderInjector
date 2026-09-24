#pragma once

#include "ShaderConfiguration/ApplyResult.h"
#include "ShaderConfiguration/ShaderConfiguration.h"

namespace DatabaseShaderConfigurations
{
	void EnsureLoaded();
	bool ReloadProperties();

	const ShaderConfiguration::DocumentDisk& GetDocument();
	ShaderConfiguration::DocumentDisk& GetEditableDocument();

	ApplyResult ApplyChanges();
} //namespace DatabaseShaderConfigurations
