#include "ShaderInjectorIO.h"

#include "../ShaderTemplates.h"

namespace ShaderInjectorIO
{
	//write embedded shader templates before compilation so these resources can always be rebuilt.
	std::string GetInternalMarkerPixelShaderSourceCodeFilePath()
	{
		return JoinPath(GetInternalDirectory(), internalMarkerPixelShaderName + extensionHLSL);
	}

	std::string GetInternalMarkerPixelShaderBlobFilePath()
	{
		return JoinPath(GetInternalDirectory(), internalMarkerPixelShaderName + extensionBLOB);
	}

	std::string GetInternalNullPixelShaderSourceCodeFilePath()
	{
		return JoinPath(GetInternalDirectory(), internalNullPixelShaderName + extensionHLSL);
	}

	std::string GetInternalNullPixelShaderBlobFilePath()
	{
		return JoinPath(GetInternalDirectory(), internalNullPixelShaderName + extensionBLOB);
	}

	std::string GetInternalMarkerComputeShaderSourceCodeFilePath()
	{
		return JoinPath(GetInternalDirectory(), internalMarkerComputeShaderName + extensionHLSL);
	}

	std::string GetInternalMarkerComputeShaderBlobFilePath()
	{
		return JoinPath(GetInternalDirectory(), internalMarkerComputeShaderName + extensionBLOB);
	}

	bool WriteInternalShaderSourceCodeToDisk(const std::string& shaderSourceFilePath, const std::string& shaderSourceText)
	{
		//restore embedded templates on disk so a missing or edited copy cannot block startup.
		if (!WriteTextFile(shaderSourceFilePath, shaderSourceText))
		{
			WriteToLogFileError("ShaderInjectorIO->WriteInternalShaderSourceCodeToDisk: failed to write file: " + shaderSourceFilePath);
			return false;
		}

		return true;
	}

	bool WriteInternalMarkerPixelShaderSourceCodeToDisk()
	{
		return WriteInternalShaderSourceCodeToDisk(GetInternalMarkerPixelShaderSourceCodeFilePath(), ShaderTemplates::internalMarkerPixelShaderSourceCode);
	}

	bool WriteInternalNullPixelShaderSourceCodeToDisk()
	{
		return WriteInternalShaderSourceCodeToDisk(GetInternalNullPixelShaderSourceCodeFilePath(), ShaderTemplates::internalNullPixelShaderSourceCode);
	}

	bool WriteInternalMarkerComputeShaderSourceCodeToDisk()
	{
		return WriteInternalShaderSourceCodeToDisk(GetInternalMarkerComputeShaderSourceCodeFilePath(), ShaderTemplates::internalMarkerComputeShaderSourceCode);
	}
} //namespace ShaderInjectorIO
