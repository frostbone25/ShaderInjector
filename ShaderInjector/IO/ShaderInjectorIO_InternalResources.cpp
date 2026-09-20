#include "ShaderInjectorIO.h"

#include "../ShaderTemplates.h"

namespace ShaderInjectorIO
{
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER INTERNAL RESOURCES |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER INTERNAL RESOURCES |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER INTERNAL RESOURCES |||||||||||||||||||||||||||||||||||||||||||||||||||||

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

	bool WriteInternalShaderSourceCodeToDisk(std::string shaderSourceFilePath, const char* shaderSourceCode)
	{
		//internal sources are regenerated from the built-in templates so tampered or missing files cannot affect injector startup.
		if (!WriteTextFile(shaderSourceFilePath, shaderSourceCode ? shaderSourceCode : ""))
		{
			WriteToLogFileError("ShaderInjectorIO->WriteInternalShaderCodeToDisk: failed to write file: " + shaderSourceFilePath);
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
}
