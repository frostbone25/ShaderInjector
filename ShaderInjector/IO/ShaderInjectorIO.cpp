//ShaderInjectorIO.cpp
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

//custom
#include "ProcessRunner.h"
#include "GUI/ShaderInjectorGUI.h"
#include "StringHelper.h"

namespace ShaderInjectorIO
{
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| IO HELPERS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| IO HELPERS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| IO HELPERS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	std::filesystem::path PathFromUtf8(const std::string& path)
	{
		std::string normalizedPath = path;

		#if !defined(_WIN32)
			//accept paths produced on Windows when running through a Unix-like host.
			std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
		#endif

		return std::filesystem::u8path(normalizedPath);
	}

	std::string PathToUtf8(const std::filesystem::path& path)
	{
		return path.u8string();
	}

	bool PathExists(const std::string& path)
	{
		std::error_code error;
		return !path.empty() && std::filesystem::exists(PathFromUtf8(path), error);
	}

	bool FileExists(const std::string& filePath)
	{
		std::error_code error;
		return !filePath.empty() && std::filesystem::is_regular_file(PathFromUtf8(filePath), error);
	}

	void DeleteFileIfExists(const std::string& filePath)
	{
		std::error_code error;

		if (!filePath.empty())
			std::filesystem::remove(PathFromUtf8(filePath), error);
	}

	bool CopyFileIfMissing(const std::string& sourcePath, const std::string& destinationPath)
	{
		if (!FileExists(sourcePath))
			return false;

		if (FileExists(destinationPath))
			return true;

		std::error_code error;

		return std::filesystem::copy_file(
			PathFromUtf8(sourcePath),
			PathFromUtf8(destinationPath),
			std::filesystem::copy_options::none,
			error);
	}

	bool WriteBinaryFile(const std::string& filePath, const void* data, size_t size)
	{
		if (!data || size == 0)
			return false;

		std::ofstream file(PathFromUtf8(filePath), std::ios::binary | std::ios::trunc);

		if (!file.is_open())
			return false;

		file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));

		return !file.fail();
	}

	bool ReadTextFile(const std::string& filePath, std::string& outText)
	{
		outText.clear();
		std::ifstream file(PathFromUtf8(filePath), std::ios::in | std::ios::binary);

		if (!file.is_open())
			return false;

		outText.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

		return !file.bad();
	}

	bool WriteTextFile(const std::string& filePath, const std::string& text)
	{
		std::ofstream file(PathFromUtf8(filePath), std::ios::out | std::ios::trunc | std::ios::binary);

		if (!file.is_open())
			return false;

		file.write(text.data(), static_cast<std::streamsize>(text.size()));

		return !file.fail();
	}

	bool WriteTextFileIfMissing(const std::string& filePath, const std::string& text)
	{
		if (FileExists(filePath))
			return true;

		return WriteTextFile(filePath, text);
	}

	bool DirectoryExists(const std::string& directoryPath)
	{
		std::error_code error;
		return !directoryPath.empty() && std::filesystem::is_directory(PathFromUtf8(directoryPath), error);
	}

	void DirectoryCreate(const std::string& directoryPath)
	{
		if (directoryPath.empty())
			return;

		std::error_code error;
		std::filesystem::create_directories(PathFromUtf8(directoryPath), error);
	}

	bool DeleteDirectoryRecursively(const std::string& directoryPath)
	{
		if (!DirectoryExists(directoryPath))
			return false;

		std::error_code error;
		std::filesystem::remove_all(PathFromUtf8(directoryPath), error);
		return !error && !PathExists(directoryPath);
	}

	bool MovePath(const std::string& sourcePath, const std::string& destinationPath, bool overwriteExisting)
	{
		if (sourcePath.empty() || destinationPath.empty())
			return false;

		const std::filesystem::path source = PathFromUtf8(sourcePath);
		const std::filesystem::path destination = PathFromUtf8(destinationPath);

		if (source == destination)
			return true;

		std::error_code error;

		if (!std::filesystem::exists(source, error))
			return false;

		error.clear();

		if (std::filesystem::exists(destination, error))
		{
			if (!overwriteExisting)
				return false;

			error.clear();

			if (std::filesystem::is_directory(destination, error))
				std::filesystem::remove_all(destination, error);
			else
				std::filesystem::remove(destination, error);

			if (error)
				return false;
		}

		error.clear();

		std::filesystem::create_directories(destination.parent_path(), error);

		if (error)
			return false;

		error.clear();

		std::filesystem::rename(source, destination, error);

		if (!error)
			return true;

		//some Wine/Proton-backed paths are fussy about rename.
		//for files, fall back to copy+remove so users can still migrate package metadata cleanly.
		error.clear();

		if (!std::filesystem::is_regular_file(source, error))
			return false;

		error.clear();

		if (!std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, error))
			return false;

		error.clear();

		std::filesystem::remove(source, error);

		return !error && std::filesystem::exists(destination, error);
	}

	bool OpenExistingPath(const std::string& path)
	{
		#if defined(_WIN32)
			const std::filesystem::path nativePath = PathFromUtf8(path);
			const HINSTANCE result = ShellExecuteW(
				nullptr,
				L"open",
				nativePath.c_str(),
				nullptr,
				nullptr,
				SW_SHOWNORMAL);
			return reinterpret_cast<INT_PTR>(result) > 32;
		#else
			const ProcessRunner::ProcessResult result = ProcessRunner::Run("/usr/bin/xdg-open", { path });
			return result.Succeeded();
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
		if (directory.empty())
			return childPath;

		if (childPath.empty())
			return directory;

		return PathToUtf8(PathFromUtf8(directory) / PathFromUtf8(childPath));
	}

	std::string DirectoryFromPath(const std::string& path)
	{
		return PathToUtf8(PathFromUtf8(path).parent_path());
	}

	std::string FileNameFromPath(const std::string& path)
	{
		return PathToUtf8(PathFromUtf8(path).filename());
	}

	std::string MakeRelativePath(const std::string& path, const std::string& baseDirectory)
	{
		if (path.empty() || baseDirectory.empty())
			return {};

		std::error_code error;
		const std::filesystem::path relativePath = std::filesystem::relative(PathFromUtf8(path), PathFromUtf8(baseDirectory), error);
		return error ? std::string() : PathToUtf8(relativePath);
	}

	bool IsAbsolutePath(const std::string& path)
	{
		if (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':' && (path[2] == '\\' || path[2] == '/'))
			return true;

		if (path.size() >= 2 && ((path[0] == '\\' && path[1] == '\\') || (path[0] == '/' && path[1] == '/')))
			return true;

		return !path.empty() && PathFromUtf8(path).is_absolute();
	}

	bool PathsEqual(const std::string& left, const std::string& right)
	{
		const std::string normalizedLeft = PathToUtf8(PathFromUtf8(left).lexically_normal());
		const std::string normalizedRight = PathToUtf8(PathFromUtf8(right).lexically_normal());

		#if defined(_WIN32)
			return StringHelper::EqualsIgnoreCase(normalizedLeft, normalizedRight);
		#else
			return normalizedLeft == normalizedRight;
		#endif
	}

	std::string SanitizeFileStem(const std::string& name)
	{
		std::string fileStem = StringHelper::TrimWhitespace(name);

		for (char& character : fileStem)
		{
			const unsigned char unsignedCharacter = static_cast<unsigned char>(character);
			const bool invalidCharacter =
				unsignedCharacter < 32 ||
				character == '<' || character == '>' || character == ':' ||
				character == '"' || character == '/' || character == '\\' ||
				character == '|' || character == '?' || character == '*';

			if (invalidCharacter)
				character = '_';
		}

		while (!fileStem.empty() && (fileStem.back() == ' ' || fileStem.back() == '.'))
			fileStem.pop_back();

		const std::string lowercaseStem = StringHelper::LowercaseAscii(fileStem);
		const bool reservedName =
			lowercaseStem == "con" || lowercaseStem == "prn" ||
			lowercaseStem == "aux" || lowercaseStem == "nul" ||
			(lowercaseStem.size() == 4 &&
				(lowercaseStem.rfind("com", 0) == 0 || lowercaseStem.rfind("lpt", 0) == 0) &&
				lowercaseStem[3] >= '1' && lowercaseStem[3] <= '9');

		if (reservedName)
			fileStem.insert(fileStem.begin(), '_');

		return fileStem;
	}

	std::string ReadRegistryString(RegistryHive hive, const std::string& subKey, const std::string& valueName)
	{
		#if defined(_WIN32)
			const HKEY rootKey = hive == RegistryHive::LocalMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
			const std::wstring wideSubKey = StringHelper::Utf8ToWide(subKey, false);
			const std::wstring wideValueName = StringHelper::Utf8ToWide(valueName, false);

			if (wideSubKey.empty())
				return {};

			DWORD requiredBytes = 0;
			const wchar_t* valueNamePointer = wideValueName.empty() ? nullptr : wideValueName.c_str();
			const DWORD acceptedTypes = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ;
			const LSTATUS sizeResult = RegGetValueW(rootKey, wideSubKey.c_str(), valueNamePointer, acceptedTypes, nullptr, nullptr, &requiredBytes);

			if (sizeResult != ERROR_SUCCESS || requiredBytes < sizeof(wchar_t))
				return {};

			std::vector<wchar_t> value(requiredBytes / sizeof(wchar_t), L'\0');
			const LSTATUS readResult = RegGetValueW(rootKey, wideSubKey.c_str(), valueNamePointer, acceptedTypes, nullptr, value.data(), &requiredBytes);

			return readResult == ERROR_SUCCESS ? StringHelper::WideToUtf8(value.data()) : std::string();
		#else
			(void)hive;
			(void)subKey;
			(void)valueName;
			return {};
		#endif
	}

	void CollectFilesByExtension(const std::string& directory, const std::string& extension, std::vector<std::string>& outFiles, bool recursive, bool includeFullPath)
	{
		if (directory.empty() || extension.empty() || !DirectoryExists(directory))
			return;

		const std::string expectedExtension = StringHelper::LowercaseAscii(extension.front() == '.' ? extension : "." + extension);
		const std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
		std::error_code error;

		auto collectEntry = [&](const std::filesystem::directory_entry& entry)
		{
			if (!entry.is_regular_file(error) || StringHelper::LowercaseAscii(PathToUtf8(entry.path().extension())) != expectedExtension)
				return;

			outFiles.push_back(PathToUtf8(includeFullPath ? entry.path() : entry.path().filename()));
		};

		if (recursive)
		{
			for (std::filesystem::recursive_directory_iterator iterator(PathFromUtf8(directory), options, error), end; iterator != end; iterator.increment(error))
			{
				if (error)
				{
					error.clear();
					continue;
				}

				collectEntry(*iterator);
			}
		}
		else
		{
			for (std::filesystem::directory_iterator iterator(PathFromUtf8(directory), options, error), end; iterator != end; iterator.increment(error))
			{
				if (error)
				{
					error.clear();
					continue;
				}

				collectEntry(*iterator);
			}
		}
	}
}
