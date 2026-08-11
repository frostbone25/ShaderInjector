#pragma once

#include <string>
#include <vector>

#include "ModifiedShader.h"

namespace DatabaseModifiedShaders
{
	void RefreshModifiedShaders();

	void EnsureModifiedShadersLoaded();

	const std::vector<ModifiedShader::PackageDisk>& GetModifiedShaders();

	const ModifiedShader::PackageDisk* FindModifiedShaderById(const std::string& modifiedShaderId);

	std::string DisplayName(const ModifiedShader::PackageDisk& modifiedShader);

	bool SetModifiedShaderEnabled(const std::string& modifiedShaderId, bool enabled);

	bool SetModifiedShaderName(const std::string& modifiedShaderId, const std::string& name);

	bool DeleteModifiedShader(const std::string& modifiedShaderId);

	bool CompileModifiedShader(const std::string& modifiedShaderId);
}
