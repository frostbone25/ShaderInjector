#pragma once

namespace ShaderInjectorInternalResources
{
	// Creates, compiles, and loads the internal marker/null shader resources.
	// The live shader blobs are updated only after every resource succeeds.
	bool Initialize();

	// Rebuilds the internal shaders using the currently selected shader models and
	// atomically replaces the live blobs used by the hooks.
	bool RecompileAndReload();
}
