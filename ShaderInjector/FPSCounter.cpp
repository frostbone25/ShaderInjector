//FPSCounter.cpp
#include "FPSCounter.h"

#include <d3d12.h>

namespace FPSCounter
{
	bool gIsFPSCounterActive = true;
	double gCurrentFramesPerSecond = 0.0;
	double gCurrentFrameTimeMilliseconds = 0.0;
	static LARGE_INTEGER gPerformanceCounterFrequency{};
	static LARGE_INTEGER gLastPerformanceCounterSample{};
	static unsigned int gFramesSinceLastSample = 0;

	void UpdateFPSCounter()
	{
		if (!gIsFPSCounterActive)
		{
			gCurrentFramesPerSecond = 0.0;
			gCurrentFrameTimeMilliseconds = 0.0;
			gFramesSinceLastSample = 0;
			gLastPerformanceCounterSample.QuadPart = 0;
			return;
		}

		if (gPerformanceCounterFrequency.QuadPart == 0)
			QueryPerformanceFrequency(&gPerformanceCounterFrequency);

		LARGE_INTEGER now{};
		QueryPerformanceCounter(&now);

		if (gLastPerformanceCounterSample.QuadPart == 0)
		{
			gLastPerformanceCounterSample = now;
			gFramesSinceLastSample = 0;
			return;
		}

		gFramesSinceLastSample++;
		const double elapsedSeconds = (double)(now.QuadPart - gLastPerformanceCounterSample.QuadPart) / (double)gPerformanceCounterFrequency.QuadPart;

		//Sample over a short window instead of every frame so the displayed value stays readable.
		if (elapsedSeconds >= 0.5)
		{
			gCurrentFramesPerSecond = (double)gFramesSinceLastSample / elapsedSeconds;
			gCurrentFrameTimeMilliseconds = (elapsedSeconds * 1000.0) / (double)gFramesSinceLastSample;
			gFramesSinceLastSample = 0;
			gLastPerformanceCounterSample = now;
		}
	}
} //namespace FPSCounter
