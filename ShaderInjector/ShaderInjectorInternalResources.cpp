#include "ShaderInjectorInternalResources.h"

#include <string>
#include <vector>

#include "Globals.h"
#include "IO/ShaderInjectorIO.h"
#include "ShaderTarget/ShaderTarget.h"
#include "StringHelper.h"

namespace ShaderInjectorInternalResources
{
	namespace
	{
		bool CompileAndLoadInternalShaders()
		{
			const bool markerPixelSourceWritten = ShaderInjectorIO::WriteInternalMarkerPixelShaderSourceCodeToDisk();
			const bool nullPixelSourceWritten = ShaderInjectorIO::WriteInternalNullPixelShaderSourceCodeToDisk();
			const bool markerComputeSourceWritten = ShaderInjectorIO::WriteInternalMarkerComputeShaderSourceCodeToDisk();

			if (!markerPixelSourceWritten || !nullPixelSourceWritten || !markerComputeSourceWritten)
			{
				ShaderInjectorIO::WriteToLogFileError("ShaderInjectorInternalResources->RecompileAndReload: failed to write one or more internal shader sources");
				return false;
			}

			const std::string pixelShaderProfile = StringHelper::ShaderProfileForType(ShaderTarget::PixelShader);
			const std::string computeShaderProfile = StringHelper::ShaderProfileForType(ShaderTarget::ComputeShader);
			std::string markerPixelBlobPath = ShaderInjectorIO::GetInternalMarkerPixelShaderBlobFilePath();
			std::string nullPixelBlobPath = ShaderInjectorIO::GetInternalNullPixelShaderBlobFilePath();
			std::string markerComputeBlobPath = ShaderInjectorIO::GetInternalMarkerComputeShaderBlobFilePath();

			const bool markerPixelCompiled = ShaderInjectorIO::CompileSourceToDXILBlob(
				ShaderInjectorIO::GetInternalMarkerPixelShaderSourceCodeFilePath(),
				pixelShaderProfile,
				"main",
				markerPixelBlobPath);

			const bool nullPixelCompiled = ShaderInjectorIO::CompileSourceToDXILBlob(
				ShaderInjectorIO::GetInternalNullPixelShaderSourceCodeFilePath(),
				pixelShaderProfile,
				"main",
				nullPixelBlobPath);

			const bool markerComputeCompiled = ShaderInjectorIO::CompileSourceToDXILBlob(
				ShaderInjectorIO::GetInternalMarkerComputeShaderSourceCodeFilePath(),
				computeShaderProfile,
				"main",
				markerComputeBlobPath);

			if (!markerPixelCompiled || !nullPixelCompiled || !markerComputeCompiled)
			{
				ShaderInjectorIO::WriteToLogFileError("ShaderInjectorInternalResources->RecompileAndReload: compilation failed" " pixelProfile=" + pixelShaderProfile + " computeProfile=" + computeShaderProfile);
				return false;
			}

			std::vector<uint8_t> markerPixelShaderBlob;
			std::vector<uint8_t> nullPixelShaderBlob;
			std::vector<uint8_t> markerComputeShaderBlob;
			const bool markerPixelLoaded = ShaderInjectorIO::LoadDXILBlobFromDisk(markerPixelBlobPath, markerPixelShaderBlob);
			const bool nullPixelLoaded = ShaderInjectorIO::LoadDXILBlobFromDisk(nullPixelBlobPath, nullPixelShaderBlob);
			const bool markerComputeLoaded = ShaderInjectorIO::LoadDXILBlobFromDisk(markerComputeBlobPath, markerComputeShaderBlob);

			if (!markerPixelLoaded || markerPixelShaderBlob.empty() ||
				!nullPixelLoaded || nullPixelShaderBlob.empty() ||
				!markerComputeLoaded || markerComputeShaderBlob.empty())
			{
				ShaderInjectorIO::WriteToLogFileError("ShaderInjectorInternalResources->RecompileAndReload: failed to load one or more compiled internal shaders");
				return false;
			}

		// Replace the live blobs only after every compile and load succeeds. This
		// prevents a partial shader-model change from leaving the hooks out of sync.
			Globals::markerPixelShaderBlob.swap(markerPixelShaderBlob);
			Globals::nullPixelShaderBlob.swap(nullPixelShaderBlob);
			Globals::markerComputeShaderBlob.swap(markerComputeShaderBlob);
			ShaderInjectorIO::WriteToLogFileSuccess("ShaderInjectorInternalResources->RecompileAndReload: applied internal shaders" " pixelProfile=" + pixelShaderProfile + " computeProfile=" + computeShaderProfile);
			return true;
		}
	}

	bool Initialize()
	{
		ShaderInjectorIO::WriteToLogFile("ShaderInjectorInternalResources->Initialize: preparing internal shader resources...");
		return CompileAndLoadInternalShaders();
	}

	bool RecompileAndReload()
	{
		return CompileAndLoadInternalShaders();
	}
}
