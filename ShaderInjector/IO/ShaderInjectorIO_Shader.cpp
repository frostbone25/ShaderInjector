#include "ShaderInjectorIO.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
	#include <Windows.h>
	#include <d3dcompiler.h>
#endif

#include "ProcessRunner.h"
#include "GUI/ShaderInjectorGUI.h"
#include "StringHelper.h"

namespace ShaderInjectorIO
{
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| SHADER |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//given the file path of a compiled shader bytecode blob, use dxc to disassemble into a somewhat readable dxil text file
	bool GenerateShaderTextDXIL(const std::string shaderBytecodeFilePath)
	{
		if (!FileExists(shaderBytecodeFilePath))
		{
			WriteToLogFileError("ShaderInjectorIO->GenerateShaderTextDXIL: error! shader bytecode file not found! " + shaderBytecodeFilePath);
			return false;
		}

		const std::string dxcPath = GetToolPathDXC();

		if (!FileExists(dxcPath))
		{
			WriteToLogFileError("ShaderInjectorIO->GenerateShaderTextDXIL: DXC executable was not found: " + dxcPath);
			return false;
		}

		std::filesystem::path outputPath = PathFromUtf8(shaderBytecodeFilePath);

		outputPath.replace_extension(extensionDXIL);

		const std::string dxilTextPath = PathToUtf8(outputPath);

		const ProcessRunner::ProcessResult processResult = ProcessRunner::Run(dxcPath, { "-dumpbin", shaderBytecodeFilePath }, dxilTextPath);

		if (!processResult.Succeeded())
		{
			DeleteFileIfExists(dxilTextPath);
			WriteToLogFileError("ShaderInjectorIO->GenerateShaderTextDXIL: DXC failed (exit = " + std::to_string(processResult.exitCode) + "): " + processResult.errorMessage);
			return false;
		}

		return FileExists(dxilTextPath);
	}

	//given raw bytecode from memory, serialize/dump it to the disk in a given directory
	bool DumpShaderBytecode(const void* bytecode, size_t size, uint64_t hash, const std::string namePrefix, const std::string& directory)
	{
		if (!bytecode || size == 0)
		{
			WriteToLogFileError("ShaderInjectorIO->DumpShaderBytecode: error! given shader bytecode is null or size is 0!");
			return false;
		}

		const std::string filename = StringHelper::Format("%016llX.bin", static_cast<unsigned long long>(hash));
		const std::string path = JoinPath(directory, namePrefix + "_" + filename);

		if (!WriteBinaryFile(path, bytecode, size))
			return false;

		return GenerateShaderTextDXIL(path);
	}

	//dxc reports compilation problems on stdout/stderr. Capturing them means a failed compile
	//can name the file, line, and reason instead of only an exit code.
	static std::string ReadCompilerDiagnostics(const std::string& compilerOutputPath)
	{
		//a single mistake can cascade into a very long error list. Keep the log file readable.
		constexpr size_t maximumDiagnosticsLength = 4000;

		std::string diagnostics;

		if (!ReadTextFile(compilerOutputPath, diagnostics))
			return {};

		const size_t lastContentCharacter = diagnostics.find_last_not_of(" \t\r\n");
		diagnostics.erase(lastContentCharacter == std::string::npos ? 0 : lastContentCharacter + 1);

		if (diagnostics.size() > maximumDiagnosticsLength)
			diagnostics = diagnostics.substr(0, maximumDiagnosticsLength) + "\n... (truncated)";

		return diagnostics;
	}

	namespace
	{
		bool UsesLegacyShaderCompiler(const std::string& shaderProfile)
		{
			return shaderProfile.size() >= 6 && shaderProfile[3] == '5' && shaderProfile[4] == '_';
		}

#if defined(_WIN32)
		class LegacyShaderIncludeHandler final : public ID3DInclude
		{
		public:
			LegacyShaderIncludeHandler(std::filesystem::path sourceDirectory, std::filesystem::path sharedIncludesDirectory)
				: sourceDirectory_(std::move(sourceDirectory)), sharedIncludesDirectory_(std::move(sharedIncludesDirectory))
			{
			}

			~LegacyShaderIncludeHandler()
			{
				for (const auto& openFile : openFileDirectories_)
					delete[] static_cast<const char*>(openFile.first);
			}

			HRESULT STDMETHODCALLTYPE Open(
				D3D_INCLUDE_TYPE,
				LPCSTR fileName,
				LPCVOID parentData,
				LPCVOID* outData,
				UINT* outBytes) override
			{
				if (!fileName || !outData || !outBytes)
					return E_INVALIDARG;

				std::vector<std::filesystem::path> candidates;
				const std::filesystem::path includePath = PathFromUtf8(fileName);

				if (includePath.is_absolute())
				{
					candidates.push_back(includePath);
				}
				else
				{
					const auto parentIt = openFileDirectories_.find(parentData);

					if (parentIt != openFileDirectories_.end())
						candidates.push_back(parentIt->second / includePath);

					candidates.push_back(sourceDirectory_ / includePath);
					candidates.push_back(sharedIncludesDirectory_ / includePath);
				}

				for (const std::filesystem::path& candidate : candidates)
				{
					std::ifstream file(candidate, std::ios::binary | std::ios::ate);

					if (!file.is_open())
						continue;

					const std::streamsize fileSize = file.tellg();

					if (fileSize < 0 || static_cast<uint64_t>(fileSize) > (std::numeric_limits<UINT>::max)())
						continue;

					file.seekg(0, std::ios::beg);
					char* data = new (std::nothrow) char[fileSize > 0 ? static_cast<size_t>(fileSize) : 1u];

					if (!data)
						return E_OUTOFMEMORY;

					if (fileSize > 0 && !file.read(data, fileSize))
					{
						delete[] data;
						continue;
					}

					*outData = data;
					*outBytes = static_cast<UINT>(fileSize);
					openFileDirectories_[data] = candidate.parent_path();
					return S_OK;
				}

				return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
			}

			HRESULT STDMETHODCALLTYPE Close(LPCVOID data) override
			{
				openFileDirectories_.erase(data);
				delete[] static_cast<const char*>(data);
				return S_OK;
			}

		private:
			std::filesystem::path sourceDirectory_;
			std::filesystem::path sharedIncludesDirectory_;
			std::unordered_map<LPCVOID, std::filesystem::path> openFileDirectories_;
		};

		using CompileFromFileFunction = HRESULT(WINAPI*)(
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
			const std::string& outputPath,
			std::string& outDiagnostics,
			std::string& outError)
		{
			outDiagnostics.clear();
			outError.clear();

			constexpr const wchar_t* compilerNames[] =
			{
				L"d3dcompiler_47.dll",
				L"d3dcompiler_46.dll",
				L"d3dcompiler_45.dll",
				L"d3dcompiler_44.dll",
				L"d3dcompiler_43.dll",
			};

			HMODULE compilerModule = nullptr;
			CompileFromFileFunction compileFromFile = nullptr;

			for (const wchar_t* compilerName : compilerNames)
			{
				compilerModule = LoadLibraryW(compilerName);

				if (!compilerModule)
					continue;

				compileFromFile = reinterpret_cast<CompileFromFileFunction>(GetProcAddress(compilerModule, "D3DCompileFromFile"));

				if (compileFromFile)
					break;

				FreeLibrary(compilerModule);
				compilerModule = nullptr;
			}

			if (!compilerModule || !compileFromFile)
			{
				outError = "No compatible d3dcompiler DLL exposing D3DCompileFromFile was found.";
				return false;
			}

			LegacyShaderIncludeHandler includeHandler(
				PathFromUtf8(DirectoryFromPath(shaderSourceFilePath)),
				PathFromUtf8(GetModifiedShadersIncludesDirectory()));
			ID3DBlob* shaderBlob = nullptr;
			ID3DBlob* errorBlob = nullptr;
			const std::wstring sourcePath = PathFromUtf8(shaderSourceFilePath).wstring();
			const HRESULT result = compileFromFile(
				sourcePath.c_str(),
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
				outDiagnostics.assign(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
				const size_t lastContentCharacter = outDiagnostics.find_last_not_of("\0 \t\r\n");
				outDiagnostics.erase(lastContentCharacter == std::string::npos ? 0 : lastContentCharacter + 1);
				constexpr size_t maximumDiagnosticsLength = 4000;

				if (outDiagnostics.size() > maximumDiagnosticsLength)
					outDiagnostics = outDiagnostics.substr(0, maximumDiagnosticsLength) + "\n... (truncated)";
			}

			if (errorBlob)
				errorBlob->Release();

			bool succeeded = SUCCEEDED(result) && shaderBlob && shaderBlob->GetBufferPointer() && shaderBlob->GetBufferSize() > 0;

			if (succeeded)
			{
				succeeded = WriteBinaryFile(outputPath, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());

				if (!succeeded)
					outError = "Could not write the compiled shader blob: " + outputPath;
			}
			else
				outError = "D3DCompileFromFile failed: " + StringHelper::FormatHRESULT(result);

			if (shaderBlob)
				shaderBlob->Release();

			FreeLibrary(compilerModule);
			return succeeded;
		}
#endif
	}

	//given HLSL source, compile SM5 profiles to DXBC and SM6 profiles to DXIL.
	bool CompileSourceToDXILBlob(
		const std::string& shaderSourceFilePath,
		const std::string& shaderProfile,
		const std::string& entryPoint,
		std::string& outBlobPath,
		ShaderSignaturePacking signaturePacking)
	{
		if (!FileExists(shaderSourceFilePath))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("ShaderInjectorIO->CompileSourceToDXILBlob: shader source file not found: " + shaderSourceFilePath);
			return false;
		}

		if (outBlobPath.empty())
		{
			std::filesystem::path blobPath = PathFromUtf8(shaderSourceFilePath);
			blobPath.replace_extension(extensionBLOB);
			outBlobPath = PathToUtf8(blobPath);
		}

		const std::string temporaryBlobPath = outBlobPath + ".compiling";
		const std::string compilerOutputPath = temporaryBlobPath + ".log";
		DeleteFileIfExists(temporaryBlobPath);
		DeleteFileIfExists(compilerOutputPath);

		std::string compilerDiagnostics;

		if (UsesLegacyShaderCompiler(shaderProfile))
		{
			#if defined(_WIN32)
				std::string compilerError;

				if (!CompileShaderModel5(
					shaderSourceFilePath,
					shaderProfile,
					entryPoint,
					temporaryBlobPath,
					compilerDiagnostics,
					compilerError))
				{
					DeleteFileIfExists(temporaryBlobPath);
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
			const std::string dxcPath = GetToolPathDXC();

			if (!FileExists(dxcPath))
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("ShaderInjectorIO->CompileSourceToDXILBlob: DXC executable was not found: " + dxcPath);
				return false;
			}

			const std::string shaderSourceDirectory = DirectoryFromPath(shaderSourceFilePath);
			const std::string modifiedShaderIncludesDirectory = GetModifiedShadersIncludesDirectory();
			std::vector<std::string> dxcArguments =
			{
				"-T", shaderProfile,
				"-E", entryPoint,
				signaturePacking == ShaderSignaturePacking::Optimized
					? "-pack-optimized"
					: "-pack-prefix-stable",
				"-I", shaderSourceDirectory,
				"-I", modifiedShaderIncludesDirectory,
				shaderSourceFilePath,
				"-Fo", temporaryBlobPath
			};

			const ProcessRunner::ProcessResult processResult = ProcessRunner::Run(dxcPath, dxcArguments, compilerOutputPath);
			compilerDiagnostics = ReadCompilerDiagnostics(compilerOutputPath);
			DeleteFileIfExists(compilerOutputPath);

			if (!processResult.Succeeded())
			{
				DeleteFileIfExists(temporaryBlobPath);
				ShaderInjectorGUI::WriteToRuntimeLogError("ShaderInjectorIO->CompileSourceToDXILBlob: DXC failed (exit = " + std::to_string(processResult.exitCode) + "): " + processResult.errorMessage);

				if (!compilerDiagnostics.empty())
					ShaderInjectorGUI::WriteToRuntimeLogError("ShaderInjectorIO->CompileSourceToDXILBlob: " + shaderSourceFilePath + " reported:\n" + compilerDiagnostics);

				return false;
			}
		}

		//a compile can succeed and still warn about something that explains an unexpected result in game.
		if (!compilerDiagnostics.empty())
			ShaderInjectorGUI::WriteToRuntimeLogWarning("ShaderInjectorIO->CompileSourceToDXILBlob: " + shaderSourceFilePath + " reported:\n" + compilerDiagnostics);

		if (!FileExists(temporaryBlobPath))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("ShaderInjectorIO->CompileSourceToDXILBlob: shader compiler reported success but did not create " + temporaryBlobPath);
			return false;
		}

		std::error_code replaceError;
		std::filesystem::copy_file(
			PathFromUtf8(temporaryBlobPath),
			PathFromUtf8(outBlobPath),
			std::filesystem::copy_options::overwrite_existing,
			replaceError);

		DeleteFileIfExists(temporaryBlobPath);

		if (replaceError)
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("ShaderInjectorIO->CompileSourceToDXILBlob: could not replace compiled blob " + outBlobPath + ": " + replaceError.message());
			return false;
		}

		if (!FileExists(outBlobPath))
		{
			ShaderInjectorGUI::WriteToRuntimeLogError("ShaderInjectorIO->CompileSourceToDXILBlob: compiled blob is missing after replacement: " + outBlobPath);
			return false;
		}

		return true;
	}

	//given the path of a compiled shader blob, load it into memory
	bool LoadDXILBlobFromDisk(const std::string& shaderBlobFilePath, std::vector<uint8_t>& outBlob)
	{
		if (!FileExists(shaderBlobFilePath))
		{
			WriteToLogFileError("ShaderInjectorIO->LoadDXILBlobFromDisk: error! shader blob file not found! " + shaderBlobFilePath);
			return false;
		}

		//clear stale bytes before loading the compiled DXIL blob into memory.
		outBlob.clear();

		std::ifstream shaderBlobFile(PathFromUtf8(shaderBlobFilePath), std::ios::binary | std::ios::ate);

		if (!shaderBlobFile.is_open())
			return false;

		const std::streamsize fileSize = shaderBlobFile.tellg();

		if (fileSize <= 0)
			return false;

		outBlob.resize(static_cast<size_t>(fileSize));
		shaderBlobFile.seekg(0, std::ios::beg);
		return static_cast<bool>(shaderBlobFile.read(reinterpret_cast<char*>(outBlob.data()), fileSize));
	}
}