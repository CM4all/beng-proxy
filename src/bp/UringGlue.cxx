// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "UringGlue.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "AllocatorPtr.hxx"

#ifdef HAVE_URING
#include "io/UringOpenStat.hxx"
#include "util/PrintException.hxx"

#include <cstdio>
#endif

#include <cerrno>

#include <fcntl.h> // for AT_EMPTY_PATH
#include <sys/stat.h>

UringGlue::UringGlue([[maybe_unused]] EventLoop &event_loop) noexcept
{
#ifdef HAVE_URING
	try {
		uring.emplace(event_loop);
	} catch (...) {
		fprintf(stderr, "Failed to initialize io_uring: ");
		PrintException(std::current_exception());
	}
#endif
}

void
UringGlue::SetVolatile() noexcept
{
#ifdef HAVE_URING
	if (uring)
		uring->SetVolatile();
#endif
}

void
UringGlue::OpenStat(AllocatorPtr alloc,
		    FileDescriptor directory,
		    const char *path,
		    UringOpenStatSuccessCallback on_success,
		    UringOpenStatErrorCallback on_error,
		    CancellablePointer &cancel_ptr) noexcept
{
#ifdef HAVE_URING
	if (uring) [[likely]] {
		UringOpenStat(*uring, alloc, directory, path,
			      on_success, on_error, cancel_ptr);
		return;
	}
#else
	(void)alloc;
	(void)cancel_ptr;
#endif

	UniqueFileDescriptor fd;

	if (fd.OpenReadOnly(directory, path)) {
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
