#pragma once

#include <string>

namespace ShaderInjectorGUI
{
	using DrawMenuFunction = void (*)();

	//pass the overlay's current state to the menu without reading unrelated globals.
	struct MainWindowContext
	{
		bool* showWindow = nullptr;
		bool injectorEnabled = false;
		bool injectorDeveloperSettings = false;
		bool* framesPerSecondCounterActive = nullptr;
		double framesPerSecond = 0.0;
		double frameTimeMilliseconds = 0.0;
		const std::string* runtimeLogText = nullptr;
		DrawMenuFunction drawMenu = nullptr;
	};
} //namespace ShaderInjectorGUI
