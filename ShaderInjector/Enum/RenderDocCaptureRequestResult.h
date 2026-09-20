#pragma once
#ifndef RENDER_DOC_CAPTURE_REQUEST_RESULT_H
#define RENDER_DOC_CAPTURE_REQUEST_RESULT_H

enum class RenderDocCaptureRequestResult
{
	Queued,
	Disabled,
	Unavailable,
	AlreadyCapturing,
	TargetUnavailable,
};

#endif