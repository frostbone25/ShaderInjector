#include "ShaderTarget/ShaderTargetInternal.h"

#include <vector>

#include "IO/ShaderInjectorIO.h"
#include "ShaderTarget/ShaderPipelineTemplateDisk.h"

namespace ShaderTarget::Internal
{
	void MakePathFieldPortable(std::string& filePath)
	{
		if (!filePath.empty())
			filePath = ShaderInjectorIO::FileNameFromPath(filePath);
	}

	void ResolvePathField(std::string& filePath, const std::string& replacementDirectory)
	{
		if (filePath.empty() || replacementDirectory.empty())
			return;

		filePath = ShaderInjectorIO::JoinPath(replacementDirectory, ShaderInjectorIO::FileNameFromPath(filePath));
	}

	std::vector<std::string*> PathFields(ShaderTarget::ShaderTargetDisk& replacement)
	{
		// These fields are stored beside the replacement JSON. Keeping them in one list
		// makes save/load path normalization consistent for current and future metadata.
		std::vector<std::string*> pathFields =
		{
			&replacement.originalShaderBlobPath,
			&replacement.jsonPath,
			&replacement.pipelineCachedBlobPath,
			&replacement.pipelineStreamBlobPath,
			&replacement.pipelineStreamMetadataPath,
			&replacement.rootSignatureBlobPath,
			&replacement.vertexShaderBlobPath,
			&replacement.pixelShaderBlobPath,
			&replacement.computeShaderBlobPath,
			&replacement.geometryShaderBlobPath,
			&replacement.hullShaderBlobPath,
			&replacement.domainShaderBlobPath,
			&replacement.amplificationShaderBlobPath,
			&replacement.meshShaderBlobPath,
		};

		for (ShaderTarget::ShaderPipelineTemplateDisk& pipelineTemplate : replacement.pipelineTemplates)
		{
			pathFields.push_back(&pipelineTemplate.pipelineCachedBlobPath);
			pathFields.push_back(&pipelineTemplate.pipelineStreamBlobPath);
			pathFields.push_back(&pipelineTemplate.pipelineStreamMetadataPath);
			pathFields.push_back(&pipelineTemplate.rootSignatureBlobPath);
			pathFields.push_back(&pipelineTemplate.vertexShaderBlobPath);
			pathFields.push_back(&pipelineTemplate.pixelShaderBlobPath);
			pathFields.push_back(&pipelineTemplate.computeShaderBlobPath);
			pathFields.push_back(&pipelineTemplate.geometryShaderBlobPath);
			pathFields.push_back(&pipelineTemplate.hullShaderBlobPath);
			pathFields.push_back(&pipelineTemplate.domainShaderBlobPath);
			pathFields.push_back(&pipelineTemplate.amplificationShaderBlobPath);
			pathFields.push_back(&pipelineTemplate.meshShaderBlobPath);
		}

		return pathFields;
	}

	void MakeReplacementPortableForDisk(ShaderTarget::ShaderTargetDisk& replacement)
	{
		for (std::string* filePath : PathFields(replacement))
			MakePathFieldPortable(*filePath);

		replacement.replacementDirectory = ".";
	}

	void ResolveReplacementPathsFromJsonLocation(ShaderTarget::ShaderTargetDisk& replacement, const std::string& jsonPath)
	{
		const std::string replacementDirectory = ShaderInjectorIO::DirectoryFromPath(jsonPath);
		replacement.replacementDirectory = replacementDirectory;

		for (std::string* filePath : PathFields(replacement))
			ResolvePathField(*filePath, replacementDirectory);

		replacement.jsonPath = jsonPath;
	}
}
