// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "UringGlue.hxx"
#include "io/FileAt.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "io/Beneath.hxx"
#include "AllocatorPtr.hxx"

#ifdef HAVE_URING
#include "io/UringStat.hxx"
#include "io/UringOpenStat.hxx"
#include "util/PrintException.hxx"

#include <cstdio>
#endif

#include <cerrno>

#include <fcntl.h> // for AT_EMPTY_PATH
#include <sys/stat.h>

UringGlue::UringGlue([[maybe_unused]] EventLoop &event_loop,
		     [[maybe_unused]] bool enable,
		     [[maybe_unused]] bool sqpoll) noexcept
{
#ifdef HAVE_URING
	if (!enable)
		return;

	unsigned flags = 0;
	if (sqpoll)
		flags |= IORING_SETUP_SQPOLL;

	try {
		uring.emplace(event_loop, 16384, flags);
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
