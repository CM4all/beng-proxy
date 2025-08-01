// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "io/uring/config.h" // for HAVE_URING
#include "io/FileDescriptor.hxx"
#include "util/BindMethod.hxx"

#ifdef HAVE_URING
#include "io/uring/Close.hxx"

#include <cassert>
#include <chrono>
#include <optional>
#endif

struct FileAt;
class CancellablePointer;
class UniqueFileDescriptor;
class EventLoop;
class AllocatorPtr;

using UringStatSuccessCallback = BoundMethod<void(const struct statx &st) noexcept>;
using UringStatErrorCallback = BoundMethod<void(int error) noexcept>;

using UringOpenStatSuccessCallback = BoundMethod<void(UniqueFileDescriptor fd, struct statx &st) noexcept>;
using UringOpenStatErrorCallback = BoundMethod<void(int error) noexcept>;

class UringGlue {
#ifdef HAVE_URING
	Uring::Queue *uring;
#endif

public:
	explicit UringGlue(EventLoop &event_loop, bool enable,
			   bool sqpoll, int sq_thread_cpu) noexcept;

#ifdef HAVE_URING
	operator bool() const noexcept {
		return uring != nullptr;
	}

	auto *get() noexcept {
		return uring;
	}

	auto &operator*() noexcept {
		assert(uring != nullptr);

		return *uring;
	}

	void Enable(Uring::Queue &_uring) noexcept {
		uring = &_uring;
	}

	void Disable() noexcept {
		uring = nullptr;
	}
#endif

	void Stat(FileAt file, int flags, unsigned mask,
		  UringStatSuccessCallback on_success,
		  UringStatErrorCallback on_error,
		  CancellablePointer &cancel_ptr) noexcept;

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

private:
	void OnEnableTimer() noexcept;
};
