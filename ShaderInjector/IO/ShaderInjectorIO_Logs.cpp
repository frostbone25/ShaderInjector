#include "ShaderInjectorIO.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

#include "../Globals.h"

namespace ShaderInjectorIO
{
	//serialize log writes and rotation so hook threads never interleave or lose entries.

	//one lock keeps complete lines together and prevents rotation from racing with a write.
	std::mutex gLogMutex;

	//verbose mode admits regular entries; status entries bypass it, while DisableLogs silences all entries.
	static bool ShouldWriteLogEntry(bool bypassVerboseFilter)
	{
		if (Globals::gDisableLogs)
			return false;

		return bypassVerboseFilter || Globals::gVerboseLog;
	}

	//format local time for each line using the thread-safe function available on this platform.
	static std::string CurrentTimestamp()
	{
		const std::time_t currentTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		std::tm localTime{};

#if defined(_WIN32)
		localtime_s(&localTime, &currentTime);
#else
		localtime_r(&currentTime, &localTime);
#endif

		char timestamp[16]{};
		if (std::strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &localTime) == 0)
			return "00:00:00";

		return timestamp;
	}

	//preserve one previous run before opening an empty current log; the caller holds gLogMutex.
	static void RotateLogFilesLocked()
	{
		//rotation can happen before Initialize, so create its directory as part of rotation.
		std::error_code error;
		std::filesystem::create_directories(PathFromUTF8(GetLogsDirectory()), error);

		const std::filesystem::path currentLogPath = PathFromUTF8(GetLogFilePath());
		const std::filesystem::path previousLogPath = PathFromUTF8(GetPreviousLogFilePath());

		//keep only one previous log by removing the older copy first.
		error.clear();
		std::filesystem::remove(previousLogPath, error);

		error.clear();

		if (std::filesystem::is_regular_file(currentLogPath, error))
		{
			//a same-directory rename is atomic when the filesystem supports it.
			error.clear();
			std::filesystem::rename(currentLogPath, previousLogPath, error);

			//copy first on filesystems where rename does not work, then remove the original.
			if (error)
			{
				error.clear();

				std::filesystem::copy_file(
					currentLogPath,
					previousLogPath,
					std::filesystem::copy_options::overwrite_existing,
					error);

				if (!error)
					std::filesystem::remove(currentLogPath, error);
			}
		}

		//truncate even when there was no old log so this process always starts with a clean file.
		std::ofstream currentLog(currentLogPath, std::ios::trunc);
	}

	static void WriteLogEntry(const std::string& logText, bool bypassVerboseFilter)
	{
		if (!ShouldWriteLogEntry(bypassVerboseFilter))
			return;

		std::lock_guard<std::mutex> logLock(gLogMutex);
		std::ofstream logFile(PathFromUTF8(GetLogFilePath()), std::ios::app);

		//logging is best effort, so a file error never interrupts rendering or hook execution.
		if (!logFile.is_open())
			return;

		logFile << '[' << CurrentTimestamp() << "] " << logText << '\n';
	}

	void RotateLogFiles()
	{
		std::lock_guard<std::mutex> logLock(gLogMutex);
		RotateLogFilesLocked();
	}

	void WriteToLogFile(const std::string& logText)
	{
		WriteLogEntry(logText, false);
	}

	void WriteToLogFileStatus(const std::string& logText)
	{
		WriteLogEntry(logText, true);
	}

	void WriteToLogFileError(const std::string& logText)
	{
		WriteLogEntry("[ERROR] " + logText, true);
	}

	void WriteToLogFileSuccess(const std::string& logText)
	{
		WriteLogEntry("[SUCCESS] " + logText, true);
	}

	void WriteToLogFileWarning(const std::string& logText)
	{
		WriteLogEntry("[WARNING] " + logText, true);
	}
} //namespace ShaderInjectorIO
