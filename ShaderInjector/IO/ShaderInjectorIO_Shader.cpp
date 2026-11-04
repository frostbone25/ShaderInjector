#include "ShaderInjectorIO.h"
#include "LegacyShaderIncludeHandler.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#include <d3dcompiler.h>
#include <dxcapi.h>
#endif

#include "ShaderConfiguration/CompileConfiguration.h"
#include "ShaderConfiguration/DatabaseShaderConfigurations.h"
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
		using D3DCompileFunction = HRESULT(WINAPI*)(
			LPCVOID,
			SIZE_T,
			LPCSTR,
			const D3D_SHADER_MACRO*,
			ID3DInclude*,
			LPCSTR,
			LPCSTR,
			UINT,
			UINT,
			ID3DBlob**,
			ID3DBlob**);

		bool PrepareConfiguredSource(
			const std::string& shaderSourceFilePath,
			const std::shared_ptr<const ShaderConfiguration::CompileConfiguration>& configuration,
			std::string& outSourceText,
			std::string& outError)
		{
			std::string sourceText;

			if (!ReadTextFile(shaderSourceFilePath, sourceText))
			{
				outError = "Could not read shader source: " + shaderSourceFilePath;
				return false;
			}

			outSourceText = std::move(sourceText);

			if (!configuration)
				return true;

			const std::string relativeSourcePath = MakeRelativePath(shaderSourceFilePath, GetModifiedShadersDirectory());
			std::string preparedSourceText;

			if (!configuration->PrepareSource(relativeSourcePath, outSourceText, preparedSourceText, outError))
				return false;

			outSourceText = std::move(preparedSourceText);
			return true;
		}

		std::vector<std::string> CompilerDefinesWithConfiguration(
			const std::vector<std::string>& defines,
			const std::shared_ptr<const ShaderConfiguration::CompileConfiguration>& configuration)
		{
			std::vector<std::string> compilerDefines = defines;

			if (configuration)
			{
				const std::vector<std::string>& configurationDefines = configuration->GetDefines();
				compilerDefines.insert(compilerDefines.end(), configurationDefines.begin(), configurationDefines.end());
			}

			return compilerDefines;
		}

		bool CompileShaderModel5(
			const std::string& shaderSourceFilePath,
			const std::string& shaderProfile,
			const std::string& entryPoint,
			const std::string& compiledBlobFilePath,
			const std::vector<std::string>& defines,
			const std::shared_ptr<const ShaderConfiguration::CompileConfiguration>& configuration,
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
			D3DCompileFunction compileShader = nullptr;

			for (const wchar_t* compilerLibraryName : compilerLibraryNames)
			{
				compilerModule = LoadLibraryW(compilerLibraryName);

				if (!compilerModule)
					continue;

				compileShader = reinterpret_cast<D3DCompileFunction>(GetProcAddress(compilerModule, "D3DCompile"));

				if (compileShader)
					break;

				FreeLibrary(compilerModule);
				compilerModule = nullptr;
			}

			if (!compilerModule || !compileShader)
			{
				compilerErrorOutput = "No compatible d3dcompiler DLL exposing D3DCompile was found.";
				return false;
			}

			LegacyShaderIncludeHandler includeHandler(
				PathFromUTF8(DirectoryFromPath(shaderSourceFilePath)),
				PathFromUTF8(GetModifiedShadersIncludesDirectory()),
				configuration);

			std::string sourceText;

			if (!PrepareConfiguredSource(shaderSourceFilePath, configuration, sourceText, compilerErrorOutput))
			{
				FreeLibrary(compilerModule);
				return false;
			}

			const std::vector<std::string> compilerDefines = CompilerDefinesWithConfiguration(defines, configuration);

			//each entry is "NAME=VALUE"; the macro array points into these strings, which outlive the call.
			std::vector<std::string> macroNames;
			std::vector<std::string> macroValues;
			macroNames.reserve(compilerDefines.size());
			macroValues.reserve(compilerDefines.size());

			for (const std::string& define : compilerDefines)
			{
				const size_t separator = define.find('=');

				if (separator == std::string::npos)
				{
					macroNames.push_back(define);
					macroValues.emplace_back();
					continue;
				}

				macroNames.push_back(define.substr(0, separator));
				macroValues.push_back(define.substr(separator + 1));
			}

			//the macro array must end with a null entry.
			std::vector<D3D_SHADER_MACRO> macros;
			macros.reserve(compilerDefines.size() + 1);

			for (size_t defineIndex = 0; defineIndex < compilerDefines.size(); ++defineIndex)
				macros.push_back({ macroNames[defineIndex].c_str(), macroValues[defineIndex].c_str() });

			macros.push_back({ nullptr, nullptr });

			ID3DBlob* shaderBlob = nullptr;
			ID3DBlob* errorBlob = nullptr;
			//the compiler receives transformed bytes in memory while diagnostics retain the original source path.
			const HRESULT compilationResult = compileShader(
				sourceText.data(),
				sourceText.size(),
				shaderSourceFilePath.c_str(),
				macros.data(),
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
				const size_t lastContentCharacter = compilerDiagnosticsOutput.find_last_not_of("\0 \t\r\n", std::string::npos, 5);

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
				compilerErrorOutput = "D3DCompile failed: " + StringHelper::FormatHRESULT(compilationResult);

			if (!compilationSucceeded && !includeHandler.GetSourcePreparationError().empty())
				compilerErrorOutput += ": " + includeHandler.GetSourcePreparationError();

			if (shaderBlob)
				shaderBlob->Release();

			FreeLibrary(compilerModule);
			return compilationSucceeded;
		}

		struct DXCRuntime
		{
			IDxcCompiler3* compiler = nullptr;
			IDxcUtils* utilities = nullptr;
			std::string error;

			explicit DXCRuntime(DxcCreateInstanceProc createCompilerInstance)
			{
				if (!createCompilerInstance)
				{
					error = "Could not load DXC library or its DxcCreateInstance export: " + GetToolPathDXCompiler();
					return;
				}

				HRESULT result = createCompilerInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));

				if (FAILED(result))
				{
					error = "Could not create IDxcCompiler3: " + StringHelper::FormatHRESULT(result);
					return;
				}

				result = createCompilerInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utilities));

				if (FAILED(result))
					error = "Could not create IDxcUtils: " + StringHelper::FormatHRESULT(result);
			}

			~DXCRuntime()
			{
				if (utilities)
					utilities->Release();

				if (compiler)
					compiler->Release();
			}
		};

		struct DXCModule
		{
			HMODULE compilerModule = nullptr;
			DxcCreateInstanceProc createCompilerInstance = nullptr;

			DXCModule()
			{
				compilerModule = LoadLibraryW(PathFromUTF8(GetToolPathDXCompiler()).wstring().c_str());

				if (compilerModule)
					createCompilerInstance = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(compilerModule, "DxcCreateInstance"));
			}
		};

		DXCRuntime& GetDXCRuntime()
		{
			//the DLL stays loaded, while each worker thread owns the DXC interfaces it uses.
			static DXCModule compilerModuleState;
			thread_local DXCRuntime compilerRuntime(compilerModuleState.createCompilerInstance);
			return compilerRuntime;
		}

		class ConfiguredDXCIncludeHandler final : public IDxcIncludeHandler
		{
			std::atomic<ULONG> referenceCount{ 1 };
			IDxcUtils* utilities = nullptr;
			IDxcIncludeHandler* defaultHandler = nullptr;
			std::shared_ptr<const ShaderConfiguration::CompileConfiguration> configuration;
			std::filesystem::path sourceDirectory;
			std::filesystem::path sharedIncludesDirectory;
			std::mutex errorMutex;
			std::string sourcePreparationError;

		  public:
			ConfiguredDXCIncludeHandler(
				IDxcUtils* compilerUtilities,
				IDxcIncludeHandler* compilerDefaultHandler,
				std::shared_ptr<const ShaderConfiguration::CompileConfiguration> configurationSnapshot,
				std::filesystem::path sourceDirectoryPath,
				std::filesystem::path sharedIncludesDirectoryPath)
				: utilities(compilerUtilities),
				  defaultHandler(compilerDefaultHandler),
				  configuration(std::move(configurationSnapshot)),
				  sourceDirectory(std::move(sourceDirectoryPath)),
				  sharedIncludesDirectory(std::move(sharedIncludesDirectoryPath))
			{
				utilities->AddRef();
				defaultHandler->AddRef();
			}

			~ConfiguredDXCIncludeHandler()
			{
				defaultHandler->Release();
				utilities->Release();
			}

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceID, void** outputInterface) override
			{
				if (!outputInterface)
					return E_POINTER;

				*outputInterface = nullptr;

				if (IsEqualIID(interfaceID, IID_IUnknown) || IsEqualIID(interfaceID, __uuidof(IDxcIncludeHandler)))
				{
					*outputInterface = static_cast<IDxcIncludeHandler*>(this);
					AddRef();
					return S_OK;
				}

				return E_NOINTERFACE;
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return ++referenceCount;
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG remainingReferences = --referenceCount;

				if (remainingReferences == 0)
					delete this;

				return remainingReferences;
			}

			HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR fileName, IDxcBlob** outputIncludeSource) override
			{
				if (!fileName || !outputIncludeSource)
					return E_INVALIDARG;

				*outputIncludeSource = nullptr;
				IDxcBlob* loadedInclude = nullptr;
				const HRESULT loadResult = defaultHandler->LoadSource(fileName, &loadedInclude);

				if (FAILED(loadResult) || !loadedInclude)
					return loadResult;

				if (!configuration)
				{
					*outputIncludeSource = loadedInclude;
					return S_OK;
				}

				IDxcBlobUtf8* utf8Include = nullptr;
				HRESULT result = utilities->GetBlobAsUtf8(loadedInclude, &utf8Include);
				loadedInclude->Release();

				if (FAILED(result) || !utf8Include)
					return result;

				std::string includePath = PathToUTF8(std::filesystem::path(fileName));

				if (!IsAbsolutePath(includePath))
				{
					const std::filesystem::path sourceCandidate = sourceDirectory / PathFromUTF8(includePath);
					const std::filesystem::path sharedIncludeCandidate = sharedIncludesDirectory / PathFromUTF8(includePath);

					if (FileExists(PathToUTF8(sourceCandidate)))
						includePath = PathToUTF8(sourceCandidate);
					else if (FileExists(PathToUTF8(sharedIncludeCandidate)))
						includePath = PathToUTF8(sharedIncludeCandidate);
				}

				const std::string relativeIncludePath = MakeRelativePath(includePath, GetModifiedShadersDirectory());
				const std::string includeText(static_cast<const char*>(utf8Include->GetBufferPointer()), utf8Include->GetBufferSize());
				utf8Include->Release();
				std::string preparedIncludeText;
				std::string preparationError;

				if (!configuration->PrepareSource(relativeIncludePath, includeText, preparedIncludeText, preparationError))
				{
					std::lock_guard<std::mutex> lock(errorMutex);
					sourcePreparationError = preparationError;
					return E_FAIL;
				}

				if (preparedIncludeText.size() > (std::numeric_limits<UINT32>::max)())
					return E_FAIL;

				IDxcBlobEncoding* preparedInclude = nullptr;
				result = utilities->CreateBlob(
					preparedIncludeText.data(),
					static_cast<UINT32>(preparedIncludeText.size()),
					DXC_CP_UTF8,
					&preparedInclude);

				if (FAILED(result))
					return result;

				*outputIncludeSource = preparedInclude;
				return S_OK;
			}

			std::string GetSourcePreparationError()
			{
				std::lock_guard<std::mutex> lock(errorMutex);
				return sourcePreparationError;
			}
		};

		bool CompileWithDXC(
			const std::string& shaderSourceFilePath,
			const std::string& shaderProfile,
			const std::string& entryPoint,
			const std::string& compiledBlobFilePath,
			ShaderSignaturePacking signaturePacking,
			const std::vector<std::string>& defines,
			const std::vector<std::string>& extraCompilerArguments,
			const std::shared_ptr<const ShaderConfiguration::CompileConfiguration>& configuration,
			std::string& compilerDiagnosticsOutput,
			std::string& compilerErrorOutput)
		{
			compilerDiagnosticsOutput.clear();
			compilerErrorOutput.clear();
			DXCRuntime& compilerRuntime = GetDXCRuntime();

			if (!compilerRuntime.compiler || !compilerRuntime.utilities)
			{
				compilerErrorOutput = compilerRuntime.error;
				return false;
			}

			std::string sourceText;

			if (!PrepareConfiguredSource(shaderSourceFilePath, configuration, sourceText, compilerErrorOutput))
				return false;

			const std::vector<std::string> compilerDefines = CompilerDefinesWithConfiguration(defines, configuration);
			const std::string shaderSourceDirectory = DirectoryFromPath(shaderSourceFilePath);
			const std::string sharedIncludesDirectory = GetModifiedShadersIncludesDirectory();
			const char* signaturePackingArgument = "-pack-prefix-stable";

			if (signaturePacking == ShaderSignaturePacking::Optimized)
				signaturePackingArgument = "-pack-optimized";

			std::vector<std::string> shaderCompilerArguments =
			{
				"-T", shaderProfile,
				"-E", entryPoint,
				signaturePackingArgument,
				"-I", shaderSourceDirectory,
				"-I", sharedIncludesDirectory,
				shaderSourceFilePath
			};

			for (const std::string& define : compilerDefines)
			{
				shaderCompilerArguments.push_back("-D");
				shaderCompilerArguments.push_back(define);
			}

			shaderCompilerArguments.insert(shaderCompilerArguments.end(), extraCompilerArguments.begin(), extraCompilerArguments.end());
			std::vector<std::wstring> wideArguments;
			std::vector<LPCWSTR> argumentPointers;
			wideArguments.reserve(shaderCompilerArguments.size());
			argumentPointers.reserve(shaderCompilerArguments.size());

			for (const std::string& argument : shaderCompilerArguments)
				wideArguments.push_back(PathFromUTF8(argument).wstring());

			for (const std::wstring& argument : wideArguments)
				argumentPointers.push_back(argument.c_str());

			IDxcIncludeHandler* defaultIncludeHandler = nullptr;
			HRESULT result = compilerRuntime.utilities->CreateDefaultIncludeHandler(&defaultIncludeHandler);

			if (FAILED(result) || !defaultIncludeHandler)
			{
				compilerErrorOutput = "Could not create DXC's default include handler: " + StringHelper::FormatHRESULT(result);
				return false;
			}

			auto* includeHandler = new (std::nothrow) ConfiguredDXCIncludeHandler(
				compilerRuntime.utilities,
				defaultIncludeHandler,
				configuration,
				PathFromUTF8(shaderSourceDirectory),
				PathFromUTF8(sharedIncludesDirectory));
			defaultIncludeHandler->Release();

			if (!includeHandler)
			{
				compilerErrorOutput = "Could not allocate DXC include handler.";
				return false;
			}

			DxcBuffer sourceBuffer{};
			sourceBuffer.Ptr = sourceText.data();
			sourceBuffer.Size = sourceText.size();
			sourceBuffer.Encoding = DXC_CP_UTF8;
			IDxcResult* compileResult = nullptr;
			result = compilerRuntime.compiler->Compile(
				&sourceBuffer,
				argumentPointers.data(),
				static_cast<UINT32>(argumentPointers.size()),
				includeHandler,
				IID_PPV_ARGS(&compileResult));

			if (FAILED(result) || !compileResult)
			{
				compilerErrorOutput = "IDxcCompiler3::Compile failed: " + StringHelper::FormatHRESULT(result);
				const std::string includePreparationError = includeHandler->GetSourcePreparationError();

				if (!includePreparationError.empty())
					compilerErrorOutput += ": " + includePreparationError;

				includeHandler->Release();
				return false;
			}

			const std::string includePreparationError = includeHandler->GetSourcePreparationError();
			includeHandler->Release();

			IDxcBlobUtf8* diagnostics = nullptr;
			compileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&diagnostics), nullptr);

			if (diagnostics && diagnostics->GetBufferPointer() && diagnostics->GetBufferSize() > 0)
				compilerDiagnosticsOutput.assign(diagnostics->GetStringPointer(), diagnostics->GetStringLength());

			if (diagnostics)
				diagnostics->Release();

			while (!compilerDiagnosticsOutput.empty() &&
				   (compilerDiagnosticsOutput.back() == '\0' || std::isspace(static_cast<unsigned char>(compilerDiagnosticsOutput.back()))))
				compilerDiagnosticsOutput.pop_back();

			constexpr size_t maximumDiagnosticsLength = 4000;

			if (compilerDiagnosticsOutput.size() > maximumDiagnosticsLength)
				compilerDiagnosticsOutput = compilerDiagnosticsOutput.substr(0, maximumDiagnosticsLength) + "\n... (truncated)";

			HRESULT compilationStatus = E_FAIL;
			compileResult->GetStatus(&compilationStatus);

			if (FAILED(compilationStatus))
			{
				compilerErrorOutput = "DXC compilation failed: " + StringHelper::FormatHRESULT(compilationStatus);

				if (!includePreparationError.empty())
					compilerErrorOutput += ": " + includePreparationError;

				compileResult->Release();
				return false;
			}

			IDxcBlob* shaderBlob = nullptr;
			result = compileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
			compileResult->Release();

			if (FAILED(result) || !shaderBlob || !shaderBlob->GetBufferPointer() || shaderBlob->GetBufferSize() == 0)
			{
				compilerErrorOutput = "DXC succeeded without producing shader object output.";
				if (shaderBlob)
					shaderBlob->Release();
				return false;
			}

			const bool wroteBlob = WriteBinaryFile(compiledBlobFilePath, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());
			shaderBlob->Release();

			if (!wroteBlob)
				compilerErrorOutput = "Could not write the compiled shader blob: " + compiledBlobFilePath;

			return wroteBlob;
		}
#endif
	} //namespace

	//compile SM5 sources with the legacy compiler and newer shader models with DXC.
	bool CompileSourceToDXILBlob(
		const std::string& shaderSourceFilePath,
		const std::string& shaderProfile,
		const std::string& entryPoint,
		std::string& compiledBlobFilePath,
		ShaderSignaturePacking signaturePacking,
		const std::vector<std::string>& defines,
		const std::vector<std::string>& extraCompilerArguments,
		std::shared_ptr<const ShaderConfiguration::CompileConfiguration> configuration)
	{
		if (!configuration)
			configuration = DatabaseShaderConfigurations::GetAppliedConfiguration();

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
					defines,
					configuration,
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
#if defined(_WIN32)
			std::string compilerError;

			if (!CompileWithDXC(
					shaderSourceFilePath,
					shaderProfile,
					entryPoint,
					temporaryBlobFilePath,
					signaturePacking,
					defines,
					extraCompilerArguments,
					configuration,
					compilerDiagnostics,
					compilerError))
			{
				DeleteFileIfExists(temporaryBlobFilePath);
				ShaderInjectorGUI::WriteToRuntimeLogError("ShaderInjectorIO->CompileSourceToDXILBlob: DXC failed: " + compilerError);

				if (!compilerDiagnostics.empty())
					ShaderInjectorGUI::WriteToRuntimeLogError("ShaderInjectorIO->CompileSourceToDXILBlob: " + shaderSourceFilePath + " reported:\n" + compilerDiagnostics);

				return false;
			}
#else
			if (configuration && !configuration->GetDefines().empty())
			{
				ShaderInjectorGUI::WriteToRuntimeLogError("ShaderInjectorIO->CompileSourceToDXILBlob: source configuration properties require the Windows in-memory DXC backend.");
				return false;
			}

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
				"-Fo", temporaryBlobFilePath
			};

			for (const std::string& define : defines)
			{
				shaderCompilerArguments.push_back("-D");
				shaderCompilerArguments.push_back(define);
			}

			//package-specific DXC switches, e.g. -enable-16bit-types for sources that use native 16-bit types.
			shaderCompilerArguments.insert(shaderCompilerArguments.end(), extraCompilerArguments.begin(), extraCompilerArguments.end());

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
#endif
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
