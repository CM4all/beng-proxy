// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "io/FileDescriptor.hxx"

#ifdef HAVE_URING
#include "event/uring/Manager.hxx"
#include "io/uring/Close.hxx"

#include <optional>
#endif

class EventLoop;

class UringGlue {
#ifdef HAVE_URING
	std::optional<Uring::Manager> uring;
#endif

public:
	void Init(EventLoop &event_loop) noexcept;
	void SetVolatile() noexcept;

#ifdef HAVE_URING
	operator bool() const noexcept {
		return uring.has_value();
	}

	auto *get() noexcept {
		return uring ? &uring.value() : nullptr;
	}

	auto &operator*() noexcept {
		return *uring;
	}
#endif

	void Close(FileDescriptor fd) noexcept {
#ifdef HAVE_URING
		Uring::Close(get(), fd);
#else
		fd.Close();
#endif
	}
};
