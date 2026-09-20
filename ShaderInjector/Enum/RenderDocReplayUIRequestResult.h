#pragma once
#ifndef RENDER_DOC_REPLAY_UI_REQUEST_RESULT_H
#define RENDER_DOC_REPLAY_UI_REQUEST_RESULT_H

enum class RenderDocReplayUIRequestResult
{
	Launched,
	AlreadyConnected,
	Disabled,
	Unavailable,
	LaunchFailed,
};

#endif