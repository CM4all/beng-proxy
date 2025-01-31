// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "UringGlue.hxx"
#include "io/FileAt.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "io/Beneath.hxx"
#include "AllocatorPtr.hxx"

#ifdef HAVE_URING
#include "event/Loop.hxx"
#include "io/UringStat.hxx"
#include "io/UringOpenStat.hxx"
#include "io/uring/Queue.hxx"
#include "util/PrintException.hxx"

#include <cstdio>
#endif

#include <cassert>
#include <cerrno>

#include <fcntl.h> // for AT_EMPTY_PATH
#include <sys/stat.h>

UringGlue::UringGlue([[maybe_unused]] EventLoop &event_loop,
		     [[maybe_unused]] bool enable,
		     [[maybe_unused]] bool sqpoll,
		     [[maybe_unused]] int sq_thread_cpu) noexcept
{
#ifdef HAVE_URING
	if (!enable)
		return;

	struct io_uring_params params{
		.flags = IORING_SETUP_SINGLE_ISSUER,
	};

	if (sqpoll) {
		params.flags |= IORING_SETUP_SQPOLL;

		if (sq_thread_cpu >= 0) {
			params.flags |= IORING_SETUP_SQ_AFF;
			params.sq_thread_cpu = sq_thread_cpu;
		}
	} else
		/* not compatible with IORING_SETUP_SQPOLL */
		params.flags |= IORING_SETUP_COOP_TASKRUN;

	try {
		event_loop.EnableUring(16384, params);
	} catch (...) {
		fprintf(stderr, "Failed to initialize io_uring: ");
		PrintException(std::current_exception());
		return;
	}

	uring = event_loop.GetUring();
	assert(uring != nullptr);

	try {
		/* limit the number of io_uring worker threads; having
		   too many only leads to lock contention inside the
		   kernel */
		// TODO make configurable?
		uring->SetMaxWorkers(64, 64);
	} catch (...) {
		fprintf(stderr, "Failed to set up io_uring: ");
		PrintException(std::current_exception());
	}
#endif
}

void
UringGlue::Stat(FileAt file, int flags, unsigned mask,
		UringStatSuccessCallback on_success,
		UringStatErrorCallback on_error,
		CancellablePointer &cancel_ptr) noexcept
{
#ifdef HAVE_URING
	if (uring) [[likely]] {
		UringStat(*uring, file, flags, mask,
			  on_success, on_error, cancel_ptr);
		return;
	}
#else
	(void)cancel_ptr;
#endif

	struct statx st;
	if (statx(file.directory.Get(), file.name, flags, mask, &st) == 0)
		on_success(st);
	else
		on_error(errno);
}

static UniqueFileDescriptor
TryOpenMaybeBeneath(FileAt file) noexcept
{
	if (file.directory.IsDefined())
		return TryOpenReadOnlyBeneath(file);

	UniqueFileDescriptor fd;
	fd.OpenReadOnly(file.name);
	return fd;
}

void
UringGlue::OpenStat(AllocatorPtr alloc, FileAt file,
		    UringOpenStatSuccessCallback on_success,
		    UringOpenStatErrorCallback on_error,
		    CancellablePointer &cancel_ptr) noexcept
{
#ifdef HAVE_URING
	if (uring) [[likely]] {
		UringOpenStat(*uring, alloc, file,
			      on_success, on_error, cancel_ptr);
		return;
	}
#else
	(void)alloc;
	(void)cancel_ptr;
#endif

	if (auto fd = TryOpenMaybeBeneath(file); fd.IsDefined()) {
		struct statx st;
		if (statx(fd.Get(), "", AT_EMPTY_PATH,
			  STATX_TYPE|STATX_MTIME|STATX_INO|STATX_SIZE,
			  &st) == 0) {
			on_success(std::move(fd), st);
			return;
		}
	}

	on_error(errno);
}
