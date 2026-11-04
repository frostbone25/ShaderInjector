#pragma once

#include <memory>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "ShaderConfiguration/ConfigurationProgressSnapshot.h"

namespace ShaderConfiguration
{
	class ConfigurationProgressTask;

	class ConfigurationProgress final : public std::enable_shared_from_this<ConfigurationProgress>
	{
		mutable std::mutex progressMutex;
		ConfigurationProgressSnapshot snapshot;
		size_t activeTaskCount = 0;
		bool dispatchFinished = false;
		bool compilationBegun = false;
		std::atomic<bool> cancellationRequested = false;
		uint64_t operationID = 0;

		void CompleteTask(bool failed, bool canceled, bool priority);
		void PromoteTask();
		friend class ConfigurationProgressTask;

	  public:
		ConfigurationProgress();
		uint64_t GetOperationID() const;
		ConfigurationProgressSnapshot GetSnapshot() const;
		void ReportPreparation(size_t completedTaskCount, size_t totalTaskCount, const std::string& stage);
		void BeginCompiling();
		std::shared_ptr<ConfigurationProgressTask> BeginTask(bool priority = false);
		void FinishDispatch();
		void FinishWithError(const std::string& errorMessage);
		void Cancel();
		bool IsCancellationRequested() const;
	};
} //namespace ShaderConfiguration
