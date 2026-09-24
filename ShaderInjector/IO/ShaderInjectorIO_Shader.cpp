#include "ShaderInjectorIO.h"
#include "LegacyShaderIncludeHandler.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#include <d3dcompiler.h>
#endif

#include "GUI/ShaderInjectorGUI.h"
#include "StringHelper.h"

namespace ShaderInjectorIO
{
	//the shader helpers share one path for choosing a compiler, collecting diagnostics, and saving the resulting bytecode.

	//disassemble a compiled shader blob so the DXIL instructions can be inspected as text.
	bool GenerateShaderTextDXIL(const std::string& shaderBytecodeFilePath)
	{
		if (!FileExists(shaderBytecodeFilePath))
		{
			WriteToLogFileError("ShaderInjectorIO->GenerateShaderTextDXIL: error! shader bytecode file not found! " + shaderBytecodeFilePath);
			return false;
		}

		const std::string shaderCompilerExecutablePath = GetToolPathDXC();

		if (!FileExists(shaderCompilerExecutablePath))
		{
			WriteToLogFileError("ShaderInjectorIO->GenerateShaderTextDXIL: DXC executable was not found: " + shaderCompilerExecutablePath);
			return false;
		}

		std::filesystem::path disassemblyFilePath = PathFromUTF8(shaderBytecodeFilePath);

		disassemblyFilePath.replace_extension(extensionDXIL);

		const std::string dxilTextFilePath = PathToUTF8(disassemblyFilePath);

		//write the disassembly beside the blob so each captured shader keeps its source and readable output together.
		const ProcessResult processResult = RunProcess(shaderCompilerExecutablePath, {"-dumpbin", shaderBytecodeFilePath}, dxilTextFilePath);

		if (!processResult.Succeeded())
		{
			DeleteFileIfExists(dxilTextFilePath);
			WriteToLogFileError("ShaderInjectorIO->GenerateShaderTextDXIL: DXC failed (exit = " + std::to_string(processResult.processExitCode) + "): " + processResult.errorMessage);
			return false;
		}

		return FileExists(dxilTextFilePath);
	}

	//write bytecode under its hash so repeated captures reuse a stable file name.
	bool DumpShaderBytecode(const void* shaderBytecode, size_t bytecodeSize, uint64_t shaderHash, const std::string& shaderNamePrefix, const std::string& dumpDirectory)
	{
		if (!shaderBytecode || bytecodeSize == 0)
		{
			WriteToLogFileError("ShaderInjectorIO->DumpShaderBytecode: error! given shader bytecode is null or size is 0!");
			return false;
		}

		//the stable hash keeps repeat captures from generating duplicate bytecode names.
		const std::string fileName = StringHelper::Format("%016llX.bin", static_cast<unsigned long long>(shaderHash));
		const std::string shaderBytecodeFilePath = JoinPath(dumpDirectory, shaderNamePrefix + "_" + fileName);

		if (!WriteBinaryFile(shaderBytecodeFilePath, shaderBytecode, bytecodeSize))
			return false;

		return GenerateShaderTextDXIL(shaderBytecodeFilePath);
	}

	//save compiler output so failures include source locations and warning text, not only an exit code.
	static std::string ReadCompilerDiagnostics(const std::string& compilerOutputFilePath)
	{
		//trim noisy compiler output before it reaches the runtime log.
		constexpr size_t maximumDiagnosticsLength = 4000;

		std::string compilerDiagnostics;

		if (!ReadTextFile(compilerOutputFilePath, compilerDiagnostics))
			return {};

		const size_t lastContentCharacter = compilerDiagnostics.find_last_not_of(" \t\r\n");

		if (lastContentCharacter == std::string::npos)
			compilerDiagnostics.clear();
		else
			compilerDiagnostics.erase(lastContentCharacter + 1);

		if (compilerDiagnostics.size() > maximumDiagnosticsLength)
			compilerDiagnostics = compilerDiagnostics.substr(0, maximumDiagnosticsLength) + "\n... (truncated)";

		return compilerDiagnostics;
	}

	namespace
	{
		//shader model 5 profiles use DXBC and the legacy compiler; newer profiles use DXC.
		bool UsesLegacyShaderCompiler(const std::string& shaderProfile)
		{
			return shaderProfile.size() >= 6 && shaderProfile[3] == '5' && shaderProfile[4] == '_';
		}

#if defined(_WIN32)
		using D3DCompileFromFileFunction = HRESULT(WINAPI*)(
			LPCWSTR,
			const D3D_SHADER_MACRO*,
			ID3DInclude*,
			LPCSTR,
			LPCSTR,
			UINT,
			UINT,
			ID3DBlob**,
			ID3DBlob**);

		bool CompileShaderModel5(
			const std::string& shaderSourceFilePath,
			const std::string& shaderProfile,
			const std::string& entryPoint,
			const std::string& compiledBlobFilePath,
			std::string& compilerDiagnosticsOutput,
			std::string& compilerErrorOutput)
		{
			compilerDiagnosticsOutput.clear();
			compilerErrorOutput.clear();

			//try installed compiler DLL versions from newest to oldest and use the first one with the required export.
			constexpr const wchar_t* compilerLibraryNames[] =
			{
				L"d3dcompiler_47.dll",
				L"d3dcompiler_46.dll",
				L"d3dcompiler_45.dll",
				L"d3dcompiler_44.dll",
				L"d3dcompiler_43.dll",
			};

			HMODULE compilerModule = nullptr;
			D3DCompileFromFileFunction compileShaderFromFile = nullptr;

			for (const wchar_t* compilerLibraryName : compilerLibraryNames)
			{
				compilerModule = LoadLibraryW(compilerLibraryName);

				if (!compilerModule)
					continue;

				compileShaderFromFile = reinterpret_cast<D3DCompileFromFileFunction>(GetProcAddress(compilerModule, "D3DCompileFromFile"));

				if (compileShaderFromFile)
					break;

				FreeLibrary(compilerModule);
				compilerModule = nullptr;
			}

			if (!compilerModule || !compileShaderFromFile)
			{
				compilerErrorOutput = "No compatible d3dcompiler DLL exposing D3DCompileFromFile was found.";
				return false;
			}

			LegacyShaderIncludeHandler includeHandler(
				PathFromUTF8(DirectoryFromPath(shaderSourceFilePath)),
				PathFromUTF8(GetModifiedShadersIncludesDirectory()));

			ID3DBlob* shaderBlob = nullptr;
			ID3DBlob* errorBlob = nullptr;
			const std::wstring shaderSourcePath = PathFromUTF8(shaderSourceFilePath).wstring();
			//the include handler lets this compile find local files and the injector's shared include folder.
			const HRESULT compilationResult = compileShaderFromFile(
				shaderSourcePath.c_str(),
				nullptr,
				&includeHandler,
				entryPoint.c_str(),
				shaderProfile.c_str(),
				D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
				0,
				&shaderBlob,
				&errorBlob);

			if (errorBlob && errorBlob->GetBufferPointer() && errorBlob->GetBufferSize() > 0)
			{
				compilerDiagnosticsOutput.assign(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
				const size_t lastContentCharacter = compilerDiagnosticsOutput.find_last_not_of("\0 \t\r\n");

				if (lastContentCharacter == std::string::npos)
					compilerDiagnosticsOutput.clear();
				else
					compilerDiagnosticsOutput.erase(lastContentCharacter + 1);

				constexpr size_t maximumDiagnosticsLength = 4000;

				if (compilerDiagnosticsOutput.size() > maximumDiagnosticsLength)
					compilerDiagnosticsOutput = compilerDiagnosticsOutput.substr(0, maximumDiagnosticsLength) + "\n... (truncated)";
			}

			if (errorBlob)
				errorBlob->Release();

			bool compilationSucceeded = SUCCEEDED(compilationResult) && shaderBlob && shaderBlob->GetBufferPointer() && shaderBlob->GetBufferSize() > 0;

			if (compilationSucceeded)
			{
				compilationSucceeded = WriteBinaryFile(compiledBlobFilePath, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());

				if (!compilationSucceeded)
					compilerErrorOutput = "Could not write the compiled shader blob: " + compiledBlobFilePath;
			}
			else
				compilerErrorOutput = "D3DCompileFromFile failed: " + StringHelper::FormatHRESULT(compilationResult);

			if (shaderBlob)
				shaderBlob->Release();

			FreeLibrary(compilerModule);
			return compilationSucceeded;
		}
#endif
	} //namespace

	//compile SM5 sources with the legacy compiler and newer shader models with DXC.
	bool CompileSourceToDXILBlob(
		const std::string& shaderSourceFilePath,
		const std::string& shaderProfile,
		const std::string& entryPoint,
		std::string& compiledBlobFilePath,
		ShaderSignaturePacking signaturePacking)
	{
		if (!FileExists(shaderSourceFilePath))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("ShaderInjectorIO->CompileSourceToDXILBlob: shader source file not found: " + shaderSourceFilePath);
			return false;
		}

		if (compiledBlobFilePath.empty())
		{
			std::filesystem::path compiledBlobFileSystemPath = PathFromUTF8(shaderSourceFilePath);
			compiledBlobFileSystemPath.replace_extension(extensionBLOB);
			compiledBlobFilePath = PathToUTF8(compiledBlobFileSystemPath);
		}

		//compile to temporary files so an error never replaces the last working blob.
		const std::string temporaryBlobFilePath = compiledBlobFilePath + ".compiling";
		const std::string compilerOutputFilePath = temporaryBlobFilePath + ".log";
		DeleteFileIfExists(temporaryBlobFilePath);
		DeleteFileIfExists(compilerOutputFilePath);

		std::string compilerDiagnostics;

		if (UsesLegacyShaderCompiler(shaderProfile))
		{
#if defined(_WIN32)
			std::string compilerError;

			if (!CompileShaderModel5(
					shaderSourceFilePath,
					shaderProfile,
					entryPoint,
					temporaryBlobFilePath,
					compilerDiagnostics,
					compilerError))
			{
				DeleteFileIfExists(temporaryBlobFilePath);
				ShaderInjectorGUI::WriteToRuntimeLogError("ShaderInjectorIO->CompileSourceToDXILBlob: legacy compiler failed: " + compilerError);

				if (!compilerDiagnostics.empty())
					ShaderInjectorGUI::WriteToRuntimeLogError("ShaderInjectorIO->CompileSourceToDXILBlob: " + shaderSourceFilePath + " reported:\n" + compilerDiagnostics);

				return false;
			}
#else
			ShaderInjectorGUI::WriteToRuntimeLogError("ShaderInjectorIO->CompileSourceToDXILBlob: Shader Model 5 compilation is unavailable on this platform.");
			return false;
#endif
		}
		else
		{
			const std::string shaderCompilerExecutablePath = GetToolPathDXC();

			if (!FileExists(shaderCompilerExecutablePath))
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("ShaderInjectorIO->CompileSourceToDXILBlob: DXC executable was not found: " + shaderCompilerExecutablePath);
				return false;
			}

			const std::string shaderSourceDirectory = DirectoryFromPath(shaderSourceFilePath);
			const std::string modifiedShaderIncludesDirectory = GetModifiedShadersIncludesDirectory();
			const char* signaturePackingArgument = "-pack-prefix-stable";

			if (signaturePacking == ShaderSignaturePacking::Optimized)
				signaturePackingArgument = "-pack-optimized";

			std::vector<std::string> shaderCompilerArguments =
				{
					"-T", shaderProfile,
					"-E", entryPoint,
					signaturePackingArgument,
					"-I", shaderSourceDirectory,
					"-I", modifiedShaderIncludesDirectory,
					shaderSourceFilePath,
					"-Fo", temporaryBlobFilePath};

			const ProcessResult processResult = RunProcess(shaderCompilerExecutablePath, shaderCompilerArguments, compilerOutputFilePath);
			compilerDiagnostics = ReadCompilerDiagnostics(compilerOutputFilePath);
			DeleteFileIfExists(compilerOutputFilePath);

			if (!processResult.Succeeded())
			{
				DeleteFileIfExists(temporaryBlobFilePath);
				ShaderInjectorGUI::WriteToRuntimeLogError("ShaderInjectorIO->CompileSourceToDXILBlob: DXC failed (exit = " + std::to_string(processResult.processExitCode) + "): " + processResult.errorMessage);

				if (!compilerDiagnostics.empty())
					ShaderInjectorGUI::WriteToRuntimeLogError("ShaderInjectorIO->CompileSourceToDXILBlob: " + shaderSourceFilePath + " reported:\n" + compilerDiagnostics);

				return false;
			}
		}

		//keep warnings because they can explain differences between the source and game output.
		if (!compilerDiagnostics.empty())
			ShaderInjectorGUI::WriteToRuntimeLogWarning("ShaderInjectorIO->CompileSourceToDXILBlob: " + shaderSourceFilePath + " reported:\n" + compilerDiagnostics);

		if (!FileExists(temporaryBlobFilePath))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("ShaderInjectorIO->CompileSourceToDXILBlob: shader compiler reported success but did not create " + temporaryBlobFilePath);
			return false;
		}

		std::error_code replaceError;
		std::filesystem::copy_file(
			PathFromUTF8(temporaryBlobFilePath),
			PathFromUTF8(compiledBlobFilePath),
			std::filesystem::copy_options::overwrite_existing,
			replaceError);

		DeleteFileIfExists(temporaryBlobFilePath);

		if (replaceError)
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("ShaderInjectorIO->CompileSourceToDXILBlob: could not replace compiled blob " + compiledBlobFilePath + ": " + replaceError.message());
			return false;
		}

		if (!FileExists(compiledBlobFilePath))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("ShaderInjectorIO->CompileSourceToDXILBlob: compiled blob is missing after replacement: " + compiledBlobFilePath);
			return false;
		}

		return true;
	}

	//load a compiled blob into memory for pipeline creation.
	bool LoadDXILBlobFromDisk(const std::string& shaderBlobFilePath, std::vector<uint8_t>& shaderBlobBytes)
	{
		if (!FileExists(shaderBlobFilePath))
		{
			WriteToLogFileError("ShaderInjectorIO->LoadDXILBlobFromDisk: error! shader blob file not found! " + shaderBlobFilePath);
			return false;
		}

		//clear previous bytes so a failed load cannot leave stale shader data behind.
		shaderBlobBytes.clear();

		std::ifstream shaderBlobFile(PathFromUTF8(shaderBlobFilePath), std::ios::binary | std::ios::ate);

		if (!shaderBlobFile.is_open())
			return false;

		const std::streamsize shaderBlobFileSize = shaderBlobFile.tellg();

		if (shaderBlobFileSize <= 0)
			return false;

		shaderBlobBytes.resize(static_cast<size_t>(shaderBlobFileSize));
		shaderBlobFile.seekg(0, std::ios::beg);
		return static_cast<bool>(shaderBlobFile.read(reinterpret_cast<char*>(shaderBlobBytes.data()), shaderBlobFileSize));
	}
} //namespace ShaderInjectorIO
