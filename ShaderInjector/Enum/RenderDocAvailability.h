#pragma once
#ifndef RENDER_DOC_AVAILABILITY_H
#define RENDER_DOC_AVAILABILITY_H

enum class RenderDocAvailability
{
	Disabled,
	NotAttached,
	InstallationNotFound,
	ModuleLoadFailed,
	ApiEntryPointMissing,
	ApiVersionUnsupported,
	Ready,
};

const char* RenderDocAvailabilityText(RenderDocAvailability availability);

#endif
