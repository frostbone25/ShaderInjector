#pragma once

namespace HookD3D12
{
	class ScopedPipelineActivity
	{
		bool shouldTrackActivity = false;

	public:
		explicit ScopedPipelineActivity(bool trackActivity = true);
		~ScopedPipelineActivity();

		ScopedPipelineActivity(const ScopedPipelineActivity&) = delete;
		ScopedPipelineActivity& operator=(const ScopedPipelineActivity&) = delete;
	};
}
