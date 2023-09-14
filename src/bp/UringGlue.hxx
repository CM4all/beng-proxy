// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "io/FileDescriptor.hxx"
#include "util/BindMethod.hxx"

#ifdef HAVE_URING
#include "event/uring/Manager.hxx"
#include "io/uring/Close.hxx"

#include <optional>
#endif

struct FileAt;
class CancellablePointer;
class UniqueFileDescriptor;
class EventLoop;
class AllocatorPtr;

using UringOpenStatSuccessCallback = BoundMethod<void(UniqueFileDescriptor fd, struct statx &st) noexcept>;
using UringOpenStatErrorCallback = BoundMethod<void(int error) noexcept>;

class UringGlue {
#ifdef HAVE_URING
	std::optional<Uring::Manager> uring;
#endif

public:
	explicit UringGlue(EventLoop &event_loop) noexcept;

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

	void OpenStat(AllocatorPtr alloc, FileAt file,
		      UringOpenStatSuccessCallback on_success,
		      UringOpenStatErrorCallback on_error,
		      CancellablePointer &cancel_ptr) noexcept;

	void Close(FileDescriptor fd) noexcept {
#ifdef HAVE_URING
		Uring::Close(get(), fd);
#else
		fd.Close();
#endif
	}
};
