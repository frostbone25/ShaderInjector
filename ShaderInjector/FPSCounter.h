#pragma once

namespace FPSCounter
{
	extern bool gIsFPSCounterActive;
	extern double gCurrentFramesPerSecond;
	extern double gCurrentFrameTimeMilliseconds;

	void UpdateFPSCounter();
} //namespace FPSCounter
