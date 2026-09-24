#pragma once

namespace HookD3D12
{
	class OverrideSnapshotReadScope
	{
	  public:
		OverrideSnapshotReadScope();
		~OverrideSnapshotReadScope();

		OverrideSnapshotReadScope(const OverrideSnapshotReadScope&) = delete;
		OverrideSnapshotReadScope& operator=(const OverrideSnapshotReadScope&) = delete;
	};
} //namespace HookD3D12
