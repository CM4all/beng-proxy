// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "pool/SharedPtr.hxx"

struct pool;
class EventLoop;
class UnusedIstreamPtr;
class PauseIstream;

class PauseIstreamControl {
	friend class PauseIstream;

	PauseIstream *pause;

public:
	explicit constexpr PauseIstreamControl(PauseIstream &_pause) noexcept
		:pause(&_pause) {}

	void Resume() noexcept;
};

std::pair<UnusedIstreamPtr, SharedPoolPtr<PauseIstreamControl>>
NewPauseIstream(struct pool &pool, EventLoop &event_loop,
		UnusedIstreamPtr input) noexcept;
