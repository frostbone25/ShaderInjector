#pragma once

namespace ShaderInjectorGUI
{
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| UI - MODIFIED SHADERS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| UI - MODIFIED SHADERS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| UI - MODIFIED SHADERS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	static const char* noteModifiedShadersText =
"Modified shaders are the main HLSL source code shaders. "
"The majority of the real meat are stored within 'ShaderInjector/ModifiedShaders/Includes'. "
"These packages also contain important data used during automatic shader discovery. "
"Based on loaded modified shaders the injector will use the data when catching PSOs to find shaders that match the signature of the modified shader. "
"Once a match is found, a shader target is created automatically that uses the modified shader bytecode. "
"NOTE: For shader targets to get found and created automatically, your game shader cache must be cleared/deleted. ";

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| UI - SHADER TARGETS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| UI - SHADER TARGETS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| UI - SHADER TARGETS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	static const char* noteShaderTargetsText =
"Shader targets are automatically generated endpoints to the actual PSOs within the game. "
"These packages should get created automatically during shader discovery. "
"If you don't have any shader targets, then that means that none of the modified shaders above will apply. "
"NOTE: For shader targets to get created automatically, your game shader cache must be cleared/deleted. ";

	static const char* tooltipRefreshShaderTargets =
		"Refreshes the view in the 'ShaderInjector/ShaderTargets' folder for shader targets that you created. ";

	static const char* tooltipEnableShaderTarget =
		"Enable/disables the currently selected shader replacement. ";

	static const char* tooltipDeleteShaderTarget =
		"Deletes the currently selected shader replacement (NO UNDO!)";

	//||||||||||||||||||||||||||||||||||||||||||||||||||||| UI - DEVELOPER SETTINGS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| UI - DEVELOPER SETTINGS |||||||||||||||||||||||||||||||||||||||||||||||||||||
	//||||||||||||||||||||||||||||||||||||||||||||||||||||| UI - DEVELOPER SETTINGS |||||||||||||||||||||||||||||||||||||||||||||||||||||

	static const char* noteDeveloperSettingsText =
"Shaders/PSOs can only be caught during shader compilation, which can be achieved by deleting the game shader cache. "
"During shader compilation this is where we catch new shaders being built. "
"From there we can then find a shader, and create a 'shader replacement' template for modification later inside the 'Shader Targets' menu up above. "
"YOU ONLY NEED TO DO THIS ONCE if you are trying to setup a replacement template for a shader you want to modify on first launch after deleting the game shader cache. "
"(Replacement shaders remain persistent on multiple re-launches even after shaders for the game are mostly/fully built) "
"Otherwise on multiple playthroughs if you find a new shader you want to modify (or you missed one), you will need to delete game shader cache to trigger the shader compilation again. "
"Shader replacements should remain persistent even after shader cache deletions and multiple launches, the only time they become invalid is if the hash bytecode changes. "
"\n"
"\n PLEASE READ DOCUMENTATION FOR MORE DETAILS OR HELP!!!";

	static const char* tooltipModifiedShaderCombobox =
"Selects a compatible ModifiedShader package from 'ShaderInjector/ModifiedShaders'.";

	static const char* tooltipRefreshModifiedShadersButton =
"Refreshes ModifiedShader packages after folders are added or removed.";

	static const char* tooltipShaderTargetRebuildButton =
"This applies the shader replacement (compiles selected source shader, rebuilds PSO, saves results)";

	static const char* tooltipSelectonStyle = 
"When a shader is selected it will be 'marked' in a couple of different ways, either with a blue pixel shader, or by hiding it.";

	static const char* tooltipClearSelections = 
"Clears any leftover selections leaving objects marked. ";

	static const char* tooltipStreamPipelinesHeader =
"Section that reveals all PSOs caught by the injector through 'D3D12_PIPELINE_STATE_STREAM'";

	static const char* tooltipCreateModifiedShaderTemplate =
"Creates a Modified Shader package containing template HLSL and shader-discovery metadata for the selected shader.";

	static const char* tooltipDumpBytecode =
"Dumps raw shader bytecode to the disk in 'ShaderInjector/Dumps'";
}
