#pragma once

#include <cstddef>
#include <string>

namespace ShaderConfiguration
{
	struct ConfigurationProgressSnapshot
	{
		bool finished = false;
		bool compilationBegun = false;
		bool dispatchFinished = false;
		bool cancellationRequested = false;
		bool blocksEditing = true;
		size_t completed = 0;
		size_t total = 0;
		size_t failed = 0;
		size_t canceled = 0;
		//active shader work can finish while the rest of the variant batch keeps compiling.
		size_t priorityTotal = 0;
		size_t priorityCompleted = 0;
		size_t priorityFailed = 0;
		std::string stage;
		std::string errorMessage;
	};
} //namespace ShaderConfiguration
