#include "ShaderConfiguration/ConfigurationProgress.h"

#include <atomic>
#include <utility>

#include "ShaderConfiguration/ConfigurationProgressTask.h"

namespace ShaderConfiguration
{
	namespace
	{
		std::atomic<uint64_t> gNextOperationID = 1;
	}

	ConfigurationProgress::ConfigurationProgress()
		: operationID(gNextOperationID.fetch_add(1))
	{
	}

	uint64_t ConfigurationProgress::GetOperationID() const
	{
		return operationID;
	}

	ConfigurationProgressSnapshot ConfigurationProgress::GetSnapshot() const
	{
		std::lock_guard<std::mutex> lock(progressMutex);
		ConfigurationProgressSnapshot snapshotCopy = snapshot;
		snapshotCopy.compilationBegun = compilationBegun;
		snapshotCopy.dispatchFinished = dispatchFinished;
		snapshotCopy.cancellationRequested = cancellationRequested.load(std::memory_order_acquire);
		snapshotCopy.blocksEditing = !snapshot.finished &&
			(!compilationBegun || !dispatchFinished || snapshot.priorityCompleted < snapshot.priorityTotal);
		return snapshotCopy;
	}

	void ConfigurationProgress::ReportPreparation(size_t completedTaskCount, size_t totalTaskCount, const std::string& stageName)
	{
		std::lock_guard<std::mutex> lock(progressMutex);

		if (snapshot.finished || compilationBegun)
			return;

		snapshot.completed = completedTaskCount;
		snapshot.total = totalTaskCount;
		snapshot.stage = stageName;
	}

	void ConfigurationProgress::BeginCompiling()
	{
		std::lock_guard<std::mutex> lock(progressMutex);

		if (snapshot.finished)
			return;

		compilationBegun = true;
		snapshot.completed = 0;
		snapshot.total = 0;
		snapshot.failed = 0;
		snapshot.canceled = 0;
		snapshot.priorityTotal = 0;
		snapshot.priorityCompleted = 0;
		snapshot.priorityFailed = 0;
		snapshot.errorMessage.clear();
		snapshot.stage = "Compiling and loading shaders";
	}

	std::shared_ptr<ConfigurationProgressTask> ConfigurationProgress::BeginTask(bool priority)
	{
		const std::shared_ptr<ConfigurationProgress> progressTracker = weak_from_this().lock();

		if (!progressTracker)
			return {};

		std::shared_ptr<ConfigurationProgressTask> progressTask(new ConfigurationProgressTask(progressTracker, priority));
		std::lock_guard<std::mutex> lock(progressMutex);

		if (snapshot.finished)
			return {};

		++snapshot.total;

		if (priority)
			++snapshot.priorityTotal;

		++activeTaskCount;
		return progressTask;
	}

	void ConfigurationProgress::FinishDispatch()
	{
		std::lock_guard<std::mutex> lock(progressMutex);

		if (snapshot.finished)
			return;

		dispatchFinished = true;

		if (activeTaskCount == 0)
		{
			snapshot.finished = true;
			snapshot.stage = "Finished";
		}
	}

	void ConfigurationProgress::FinishWithError(const std::string& errorMessage)
	{
		std::lock_guard<std::mutex> lock(progressMutex);

		if (snapshot.finished)
			return;

		snapshot.errorMessage = errorMessage;
		snapshot.stage = "Failed";
		snapshot.finished = true;
	}

	void ConfigurationProgress::Cancel()
	{
		std::lock_guard<std::mutex> lock(progressMutex);

		if (snapshot.finished)
			return;

		cancellationRequested.store(true, std::memory_order_release);
		dispatchFinished = true;
		snapshot.cancellationRequested = true;
		snapshot.canceled += activeTaskCount;
		snapshot.completed += activeTaskCount;
		const size_t outstandingPriorityTaskCount = snapshot.priorityTotal - snapshot.priorityCompleted;
		snapshot.priorityCompleted += outstandingPriorityTaskCount;
		snapshot.priorityFailed += outstandingPriorityTaskCount;
		activeTaskCount = 0;
		snapshot.finished = true;
		snapshot.stage = "Superseded";
	}

	bool ConfigurationProgress::IsCancellationRequested() const
	{
		return cancellationRequested.load(std::memory_order_acquire);
	}

	void ConfigurationProgress::PromoteTask()
	{
		std::lock_guard<std::mutex> lock(progressMutex);

		if (!snapshot.finished)
			++snapshot.priorityTotal;
	}

	void ConfigurationProgress::CompleteTask(bool failed, bool canceled, bool priority)
	{
		std::lock_guard<std::mutex> lock(progressMutex);

		if (snapshot.finished || activeTaskCount == 0)
			return;

		--activeTaskCount;
		++snapshot.completed;

		if (failed)
			++snapshot.failed;

		if (canceled)
			++snapshot.canceled;

		if (priority)
		{
			++snapshot.priorityCompleted;

			//discarded work is not proof that its active shader was successfully replaced.
			if (failed || canceled)
				++snapshot.priorityFailed;
		}

		if (dispatchFinished && activeTaskCount == 0)
		{
			snapshot.finished = true;
			snapshot.stage = "Finished";
		}
	}

	ConfigurationProgressTask::ConfigurationProgressTask(std::shared_ptr<ConfigurationProgress> progressTracker, bool priorityTask)
		: progress(std::move(progressTracker)), priority(priorityTask)
	{
	}

	ConfigurationProgressTask::~ConfigurationProgressTask()
	{
		if (completed.exchange(true))
			return;

		const ConfigurationProgressTaskOutcome taskOutcome = outcome.load();

		if (progress)
			progress->CompleteTask(
				taskOutcome == ConfigurationProgressTaskOutcome::Failed,
				taskOutcome == ConfigurationProgressTaskOutcome::Canceled,
				priority.load());
	}

	void ConfigurationProgressTask::MarkSucceeded()
	{
		if (!completed.load())
			outcome.store(ConfigurationProgressTaskOutcome::Succeeded);
	}

	void ConfigurationProgressTask::MarkFailed()
	{
		if (!completed.load())
			outcome.store(ConfigurationProgressTaskOutcome::Failed);
	}

	void ConfigurationProgressTask::PromotePriority()
	{
		//an on-demand variant can become active after it was added as background work.
		if (!completed.load() && !priority.exchange(true) && progress)
			progress->PromoteTask();
	}
} //namespace ShaderConfiguration
