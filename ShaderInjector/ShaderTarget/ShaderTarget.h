#pragma once

#include <string>
#include <vector>

#include "ShaderTarget/ShaderPipelineTemplateDisk.h"
#include "ShaderTarget/ShaderTargetDisk.h"
#include "ShaderTarget/ShaderInputElementDisk.h"
#include "ShaderTarget/ShaderStreamOutputDeclarationDisk.h"
#include "ShaderTarget/ShaderPipelineStreamMetadataDisk.h"

namespace ShaderTarget
{
	bool WriteShaderTargetJson(const ShaderTargetDisk& replacement);
	bool LoadShaderTargetJson(const std::string& path, ShaderTargetDisk& outReplacement);
	bool WritePipelineStreamMetadataJson(const std::string& path, const ShaderPipelineStreamMetadataDisk& metadata);
	bool LoadPipelineStreamMetadataJson(const std::string& path, ShaderPipelineStreamMetadataDisk& outMetadata);
	void CollectShaderTargetJsonFiles(const std::string& directory, std::vector<std::string>& outJsonFiles);
}
