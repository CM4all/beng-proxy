// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <utility>

class Stopwatch;
class UniqueFileDescriptor;

#ifdef ENABLE_STOPWATCH

#include <memory>

class StopwatchPtr {
protected:
	std::shared_ptr<Stopwatch> stopwatch;

public:
	StopwatchPtr() = default;
	StopwatchPtr(std::nullptr_t) noexcept {}

protected:
	StopwatchPtr(const char *name,
		     const char *suffix=nullptr) noexcept;

public:
	StopwatchPtr(Stopwatch *parent, const char *name,
		     const char *suffix=nullptr) noexcept;

	StopwatchPtr(const StopwatchPtr &parent, const char *name,
		     const char *suffix=nullptr) noexcept
		:StopwatchPtr(parent.stopwatch.get(), name, suffix) {}

	~StopwatchPtr() noexcept;

	operator bool() const noexcept {
		return stopwatch != nullptr;
	}

	void RecordEvent(const char *name) const noexcept;
};

class RootStopwatchPtr : public StopwatchPtr {
public:
	RootStopwatchPtr() = default;

	RootStopwatchPtr(const char *name, const char *suffix=nullptr) noexcept
		:StopwatchPtr(name, suffix) {}

	RootStopwatchPtr(RootStopwatchPtr &&) noexcept = default;
	RootStopwatchPtr &operator=(RootStopwatchPtr &&) noexcept = default;
};

void
stopwatch_enable(UniqueFileDescriptor fd) noexcept;

[[gnu::pure]]
bool
stopwatch_is_enabled() noexcept;

#else

class StopwatchPtr {
public:
	StopwatchPtr() = default;
	StopwatchPtr(std::nullptr_t) noexcept {}

	StopwatchPtr(const char *, const char * =nullptr) noexcept {}

	StopwatchPtr(const StopwatchPtr &, const char *,
		     const char * =nullptr) noexcept {}

	operator bool() const noexcept {
		return false;
	}

	void RecordEvent(const char *) const noexcept {}
};

using RootStopwatchPtr = StopwatchPtr;

static inline void
stopwatch_enable(UniqueFileDescriptor &&) noexcept
{
}

static inline bool
stopwatch_is_enabled() noexcept
{
	return false;
}

#endif
