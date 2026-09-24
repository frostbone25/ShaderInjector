#include "ShaderInjectorIO.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <new>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#include <d3dcompiler.h>
#include <shellapi.h>
#endif

#include "GUI/ShaderInjectorGUI.h"
#include "StringHelper.h"

namespace ShaderInjectorIO
{
	//convert UTF-8 at the filesystem boundary so callers can keep one path encoding on every platform.
	std::filesystem::path PathFromUTF8(const std::string& pathString)
	{
		std::string normalizedPath = pathString;

#if !defined(_WIN32)
		//normalize Windows separators when the same configuration is read on a Unix-like host.
		std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
#endif

		return std::filesystem::u8path(normalizedPath);
	}

	std::string PathToUTF8(const std::filesystem::path& fileSystemPath)
	{
		return fileSystemPath.u8string();
	}

	//use error_code overloads so missing paths simply report false instead of throwing.
	bool PathExists(const std::string& pathString)
	{
		std::error_code error;
		return !pathString.empty() && std::filesystem::exists(PathFromUTF8(pathString), error);
	}

	bool FileExists(const std::string& filePath)
	{
		std::error_code error;
		return !filePath.empty() && std::filesystem::is_regular_file(PathFromUTF8(filePath), error);
	}

	//remove is safe to call before creating a generated file, even when no previous copy exists.
	void DeleteFileIfExists(const std::string& filePath)
	{
		std::error_code error;

		if (!filePath.empty())
			std::filesystem::remove(PathFromUTF8(filePath), error);
	}

	bool CopyFileIfMissing(const std::string& sourcePath, const std::string& destinationPath)
	{
		//keep user-modified destination files intact; only seed files that do not exist yet.
		if (!FileExists(sourcePath))
			return false;

		if (FileExists(destinationPath))
			return true;

		std::error_code error;

		return std::filesystem::copy_file(
			PathFromUTF8(sourcePath),
			PathFromUTF8(destinationPath),
			std::filesystem::copy_options::none,
			error);
	}

	bool WriteBinaryFile(const std::string& filePath, const void* fileData, size_t dataByteCount)
	{
		if (!fileData || dataByteCount == 0)
			return false;

		std::ofstream binaryFile(PathFromUTF8(filePath), std::ios::binary | std::ios::trunc);

		if (!binaryFile.is_open())
			return false;

		binaryFile.write(static_cast<const char*>(fileData), static_cast<std::streamsize>(dataByteCount));

		return !binaryFile.fail();
	}

	bool ReadTextFile(const std::string& filePath, std::string& fileText)
	{
		fileText.clear();
		std::ifstream textFile(PathFromUTF8(filePath), std::ios::in | std::ios::binary);

		if (!textFile.is_open())
			return false;

		fileText.assign(std::istreambuf_iterator<char>(textFile), std::istreambuf_iterator<char>());

		return !textFile.bad();
	}

	bool WriteTextFile(const std::string& filePath, const std::string& fileText)
	{
		std::ofstream textFile(PathFromUTF8(filePath), std::ios::out | std::ios::trunc | std::ios::binary);

		if (!textFile.is_open())
			return false;

		textFile.write(fileText.data(), static_cast<std::streamsize>(fileText.size()));

		return !textFile.fail();
	}

	bool WriteTextFileIfMissing(const std::string& filePath, const std::string& fileText)
	{
		if (FileExists(filePath))
			return true;

		return WriteTextFile(filePath, fileText);
	}

	bool DirectoryExists(const std::string& directoryPath)
	{
		std::error_code error;
		return !directoryPath.empty() && std::filesystem::is_directory(PathFromUTF8(directoryPath), error);
	}

	void DirectoryCreate(const std::string& directoryPath)
	{
		if (directoryPath.empty())
			return;

		std::error_code error;
		std::filesystem::create_directories(PathFromUTF8(directoryPath), error);
	}

	bool DeleteDirectoryRecursively(const std::string& directoryPath)
	{
		if (!DirectoryExists(directoryPath))
			return false;

		//remove_all deletes the directory tree; check both the error and final path state before reporting success.
		std::error_code error;
		std::filesystem::remove_all(PathFromUTF8(directoryPath), error);

		return !error && !PathExists(directoryPath);
	}

	bool MovePath(const std::string& sourcePath, const std::string& destinationPath, bool overwriteExisting)
	{
		if (sourcePath.empty() || destinationPath.empty())
			return false;

		const std::filesystem::path sourceFileSystemPath = PathFromUTF8(sourcePath);
		const std::filesystem::path destinationFileSystemPath = PathFromUTF8(destinationPath);

		if (sourceFileSystemPath == destinationFileSystemPath)
			return true;

		std::error_code error;

		if (!std::filesystem::exists(sourceFileSystemPath, error))
			return false;

		error.clear();

		if (std::filesystem::exists(destinationFileSystemPath, error))
		{
			if (!overwriteExisting)
				return false;

			error.clear();

			if (std::filesystem::is_directory(destinationFileSystemPath, error))
				std::filesystem::remove_all(destinationFileSystemPath, error);
			else
				std::filesystem::remove(destinationFileSystemPath, error);

			if (error)
				return false;
		}

		error.clear();

		std::filesystem::create_directories(destinationFileSystemPath.parent_path(), error);

		if (error)
			return false;

		error.clear();

		std::filesystem::rename(sourceFileSystemPath, destinationFileSystemPath, error);

		if (!error)
			return true;

		//some Wine and Proton filesystems reject rename, so copy a file before removing the original.
		error.clear();

		if (!std::filesystem::is_regular_file(sourceFileSystemPath, error))
			return false;

		error.clear();

		if (!std::filesystem::copy_file(sourceFileSystemPath, destinationFileSystemPath, std::filesystem::copy_options::none, error))
			return false;

		error.clear();

		std::filesystem::remove(sourceFileSystemPath, error);

		return !error && std::filesystem::exists(destinationFileSystemPath, error);
	}

	//hand the native path to the desktop shell; Linux opens it through its standard desktop launcher.
	bool OpenExistingPath(const std::string& targetPath)
	{
#if defined(_WIN32)
		const std::filesystem::path nativePath = PathFromUTF8(targetPath);
		const HINSTANCE shellLaunchResult = ShellExecuteW(
			nullptr,
			L"open",
			nativePath.c_str(),
			nullptr,
			nullptr,
			SW_SHOWNORMAL);
		return reinterpret_cast<INT_PTR>(shellLaunchResult) > 32;
#else
		const ProcessResult processResult = RunProcess("/usr/bin/xdg-open", {targetPath});
		return processResult.Succeeded();
#endif
	}

	bool OpenFile(const std::string& filePath)
	{
		return FileExists(filePath) && OpenExistingPath(filePath);
	}

	bool OpenDirectory(const std::string& directoryPath)
	{
		if (!DirectoryExists(directoryPath))
			return false;

		return OpenExistingPath(directoryPath);
	}

	std::string JoinPath(const std::string& directory, const std::string& childPath)
	{
		//let filesystem::path insert the platform's separator instead of building paths with string concatenation.
		if (directory.empty())
			return childPath;

		if (childPath.empty())
			return directory;

		return PathToUTF8(PathFromUTF8(directory) / PathFromUTF8(childPath));
	}

	std::string DirectoryFromPath(const std::string& filePath)
	{
		return PathToUTF8(PathFromUTF8(filePath).parent_path());
	}

	std::string FileNameFromPath(const std::string& filePath)
	{
		return PathToUTF8(PathFromUTF8(filePath).filename());
	}

	std::string MakeRelativePath(const std::string& filePath, const std::string& baseDirectory)
	{
		if (filePath.empty() || baseDirectory.empty())
			return {};

		std::error_code error;
		const std::filesystem::path relativePath = std::filesystem::relative(PathFromUTF8(filePath), PathFromUTF8(baseDirectory), error);

		if (error)
			return {};

		return PathToUTF8(relativePath);
	}

	//recognize Windows paths even when this runs on a Unix-like host that would not parse them natively.
	bool IsAbsolutePath(const std::string& pathString)
	{
		if (pathString.size() >= 3 && std::isalpha(static_cast<unsigned char>(pathString[0])) && pathString[1] == ':' && (pathString[2] == '\\' || pathString[2] == '/'))
			return true;

		if (pathString.size() >= 2 && ((pathString[0] == '\\' && pathString[1] == '\\') || (pathString[0] == '/' && pathString[1] == '/')))
			return true;

		return !pathString.empty() && PathFromUTF8(pathString).is_absolute();
	}

	//normalize separators and dot segments first; Windows paths also ignore letter case.
	bool PathsEqual(const std::string& leftPath, const std::string& rightPath)
	{
		const std::string normalizedLeftPath = PathToUTF8(PathFromUTF8(leftPath).lexically_normal());
		const std::string normalizedRightPath = PathToUTF8(PathFromUTF8(rightPath).lexically_normal());

#if defined(_WIN32)
		return StringHelper::EqualsIgnoreCase(normalizedLeftPath, normalizedRightPath);
#else
		return normalizedLeftPath == normalizedRightPath;
#endif
	}

	std::string SanitizeFileStem(const std::string& fileStemName)
	{
		//replace filename characters that Windows rejects, even when the current host permits them.
		std::string fileStem = StringHelper::TrimWhitespace(fileStemName);

		for (char& character : fileStem)
		{
			const unsigned char unsignedCharacter = static_cast<unsigned char>(character);

			const bool invalidCharacter =
				unsignedCharacter < 32 ||
				character == '<' ||
				character == '>' ||
				character == ':' ||
				character == '"' ||
				character == '/' ||
				character == '\\' ||
				character == '|' ||
				character == '?' ||
				character == '*';

			if (invalidCharacter)
				character = '_';
		}

		while (!fileStem.empty() && (fileStem.back() == ' ' || fileStem.back() == '.'))
			fileStem.pop_back();

		//windows reserves device names even when an extension follows, so prefix those stems too.
		const std::string lowercaseStem = StringHelper::LowercaseAscii(fileStem);

		const bool reservedName =
			lowercaseStem == "con" ||
			lowercaseStem == "prn" ||
			lowercaseStem == "aux" ||
			lowercaseStem == "nul" ||
			(lowercaseStem.size() == 4 &&
			 (lowercaseStem.rfind("com", 0) == 0 || lowercaseStem.rfind("lpt", 0) == 0) &&
			 lowercaseStem[3] >= '1' && lowercaseStem[3] <= '9');

		if (reservedName)
			fileStem.insert(fileStem.begin(), '_');

		return fileStem;
	}

	std::string ReadRegistryString(RegistryHive hive, const std::string& registrySubKey, const std::string& registryValueName)
	{
#if defined(_WIN32)
		HKEY rootKey = HKEY_CURRENT_USER;

		if (hive == RegistryHive::LocalMachine)
			rootKey = HKEY_LOCAL_MACHINE;

		const std::wstring wideSubKey = StringHelper::Utf8ToWide(registrySubKey, false);
		const std::wstring wideValueName = StringHelper::Utf8ToWide(registryValueName, false);

		if (wideSubKey.empty())
			return {};

		//query the value size first, then allocate enough wide characters for the registry result.
		DWORD requiredBytes = 0;
		const wchar_t* valueNamePointer = nullptr;

		if (!wideValueName.empty())
			valueNamePointer = wideValueName.c_str();

		const DWORD acceptedTypes = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ;
		const LSTATUS sizeResult = RegGetValueW(rootKey, wideSubKey.c_str(), valueNamePointer, acceptedTypes, nullptr, nullptr, &requiredBytes);

		if (sizeResult != ERROR_SUCCESS || requiredBytes < sizeof(wchar_t))
			return {};

		std::vector<wchar_t> registryValueCharacters(requiredBytes / sizeof(wchar_t), L'\0');
		const LSTATUS readResult = RegGetValueW(rootKey, wideSubKey.c_str(), valueNamePointer, acceptedTypes, nullptr, registryValueCharacters.data(), &requiredBytes);

		if (readResult != ERROR_SUCCESS)
			return {};

		return StringHelper::WideToUtf8(registryValueCharacters.data());
#else
		(void)hive;
		(void)registrySubKey;
		(void)registryValueName;
		return {};
#endif
	}

	void CollectFilesByExtension(const std::string& directory, const std::string& extension, std::vector<std::string>& collectedFilePaths, bool includeSubdirectories, bool includeFullPath)
	{
		if (directory.empty() || extension.empty() || !DirectoryExists(directory))
			return;

		std::string expectedExtension = extension;

		if (expectedExtension.front() != '.')
			expectedExtension.insert(expectedExtension.begin(), '.');

		expectedExtension = StringHelper::LowercaseAscii(expectedExtension);
		const std::filesystem::directory_options directoryIteratorOptions = std::filesystem::directory_options::skip_permission_denied;
		std::error_code directoryIterationError;

		//normalize the suffix once, then compare each regular file without regard to letter case.
		auto collectMatchingFile = [&](const std::filesystem::directory_entry& directoryEntry)
		{
			if (!directoryEntry.is_regular_file(directoryIterationError) || StringHelper::LowercaseAscii(PathToUTF8(directoryEntry.path().extension())) != expectedExtension)
				return;

			if (includeFullPath)
				collectedFilePaths.push_back(PathToUTF8(directoryEntry.path()));
			else
				collectedFilePaths.push_back(PathToUTF8(directoryEntry.path().filename()));
		};

		//walk either this folder or its whole tree, clearing iterator errors so later files can still be checked.
		if (includeSubdirectories)
		{
			for (std::filesystem::recursive_directory_iterator directoryIterator(PathFromUTF8(directory), directoryIteratorOptions, directoryIterationError), endIterator; directoryIterator != endIterator; directoryIterator.increment(directoryIterationError))
			{
				if (directoryIterationError)
				{
					directoryIterationError.clear();
					continue;
				}

				collectMatchingFile(*directoryIterator);
			}
		}
		else
		{
			for (std::filesystem::directory_iterator directoryIterator(PathFromUTF8(directory), directoryIteratorOptions, directoryIterationError), endIterator; directoryIterator != endIterator; directoryIterator.increment(directoryIterationError))
			{
				if (directoryIterationError)
				{
					directoryIterationError.clear();
					continue;
				}

				collectMatchingFile(*directoryIterator);
			}
		}
	}
} //namespace ShaderInjectorIO
