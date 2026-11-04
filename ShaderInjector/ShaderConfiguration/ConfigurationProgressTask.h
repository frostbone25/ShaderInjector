#pragma once

#include <atomic>
#include <memory>

#include "ShaderConfiguration/ConfigurationProgressTaskOutcome.h"

namespace ShaderConfiguration
{
	class ConfigurationProgress;

	class ConfigurationProgressTask final
	{
		std::shared_ptr<ConfigurationProgress> progress;
		std::atomic<ConfigurationProgressTaskOutcome> outcome = ConfigurationProgressTaskOutcome::Canceled;
		std::atomic<bool> completed = false;
		std::atomic<bool> priority = false;

		ConfigurationProgressTask(std::shared_ptr<ConfigurationProgress> progressTracker, bool priorityTask);
		friend class ConfigurationProgress;

	  public:
		~ConfigurationProgressTask();
		void MarkSucceeded();
		void MarkFailed();
		void PromotePriority();
	};
} //namespace ShaderConfiguration
