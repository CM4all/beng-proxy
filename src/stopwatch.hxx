/*
 * Copyright 2007-2019 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "util/Compiler.h"

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

gcc_pure
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
