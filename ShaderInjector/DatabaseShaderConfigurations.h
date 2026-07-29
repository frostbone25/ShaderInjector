#pragma once

#include <cstddef>
#include <string>

#include "ShaderConfiguration.h"

namespace DatabaseShaderConfigurations
{
	struct ApplyResult
	{
		bool succeeded = false;
		size_t propertyCount = 0;
		size_t sourceFileCount = 0;
		std::string errorMessage;
	};

	void EnsureLoaded();
	bool ReloadProperties();

	const ShaderConfiguration::DocumentDisk& GetDocument();
	ShaderConfiguration::DocumentDisk& GetEditableDocument();

	ApplyResult ApplyChanges();
}
