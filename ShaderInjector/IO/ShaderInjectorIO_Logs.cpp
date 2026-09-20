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
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| LOGS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| LOGS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| LOGS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	//all writes and log rotation share this mutex.
	//this prevents concurrent hook threads from interleaving lines or rotating the file while another thread is appending to it.
	std::mutex gLogMutex;

	//informational messages are controlled by VerboseLog. 
	//warnings, errors, and success messages bypass that setting, but DisableLogs still suppresses every message.
	bool ShouldWriteLogEntry(bool forceWrite)
	{
		if (Globals::gDisableLogs)
			return false;

		return forceWrite || Globals::gVerboseLog;
	}

	//convert the current time into the short timestamp used at the beginning of every line.
	//localtime_s and localtime_r are the thread-safe variants on their respective platforms.
	std::string CurrentTimestamp()
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

	//start a fresh current log while preserving exactly one previous run. 
	//the caller must hold gLogMutex because this helper is part of the startup rotation sequence.
	void RotateLogFilesLocked()
	{
		//rotation runs before the rest of IO initialization, so create the directory here rather than assuming Initialize has already created it.
		std::error_code error;
		std::filesystem::create_directories(PathFromUtf8(GetLogsDirectory()), error);

		const std::filesystem::path currentLogPath = PathFromUtf8(GetLogFilePath());
		const std::filesystem::path previousLogPath = PathFromUtf8(GetPreviousLogFilePath());

		//there is only room for one previous log, so remove it before moving the current log.
		error.clear();
		std::filesystem::remove(previousLogPath, error);

		error.clear();
		if (std::filesystem::is_regular_file(currentLogPath, error))
		{
			//a same-directory rename is atomic and is the preferred path.
			error.clear();
			std::filesystem::rename(currentLogPath, previousLogPath, error);

			//some Wine/Proton filesystem combinations reject rename even within the same directory.
			//copy first, then remove the original as a portable fallback.
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

		//truncation guarantees that a failed or empty rotation still starts a clean current log for this process.
		std::ofstream currentLog(currentLogPath, std::ios::trunc);
	}

	void WriteLogEntry(const std::string& text, bool forceWrite)
	{
		if (!ShouldWriteLogEntry(forceWrite))
			return;

		std::lock_guard<std::mutex> lock(gLogMutex);
		std::ofstream logFile(PathFromUtf8(GetLogFilePath()), std::ios::app);

		//logging is best effort. 
		//a failed file open must never interfere with rendering or with the game's hook execution.
		if (!logFile.is_open())
			return;

		logFile << '[' << CurrentTimestamp() << "] " << text << '\n';
	}

	void RotateLogFiles()
	{
		std::lock_guard<std::mutex> lock(gLogMutex);
		RotateLogFilesLocked();
	}

	void WriteToLogFile(const std::string& text)
	{
		WriteLogEntry(text, false);
	}

	void WriteToLogFileStatus(const std::string& text)
	{
		WriteLogEntry(text, true);
	}

	void WriteToLogFileError(const std::string& text)
	{
		WriteLogEntry("[ERROR] " + text, true);
	}

	void WriteToLogFileSuccess(const std::string& text)
	{
		WriteLogEntry("[SUCCESS] " + text, true);
	}

	void WriteToLogFileWarning(const std::string& text)
	{
		WriteLogEntry("[WARNING] " + text, true);
	}
}
