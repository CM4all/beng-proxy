// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "FdIstream.hxx"
#include "istream.hxx"
#include "Handler.hxx"
#include "New.hxx"
#include "Result.hxx"
#include "lib/fmt/SystemError.hxx"
#include "io/Buffered.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "memory/fb_pool.hxx"
#include "memory/SliceFifoBuffer.hxx"
#include "event/FineTimerEvent.hxx"

#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

/**
 * If EAGAIN occurs (on NFS), we try again after 100ms.  We can't
 * check SocketEvent::READ, because the kernel always indicates VFS files as
 * "readable without blocking".
 */
static constexpr Event::Duration file_retry_timeout = std::chrono::milliseconds(100);

class FdIstream final : public Istream {
	UniqueFileDescriptor fd;

	FdType fd_type;

	/**
	 * A timer to retry reading after EAGAIN.
	 */
	FineTimerEvent retry_event;

	SliceFifoBuffer buffer;
	const char *path;

	bool direct = false;

public:
	FdIstream(struct pool &p, EventLoop &event_loop,
		  UniqueFileDescriptor &&_fd, FdType _fd_type,
		  const char *_path) noexcept
		:Istream(p),
		 fd(std::move(_fd)), fd_type(_fd_type),
		 retry_event(event_loop, BIND_THIS_METHOD(EventCallback)),
		 path(_path) {}

private:
	void TryData();
	void TryDirect();

	void TryRead() noexcept {
		try {
			if (!fd.IsDefined())
				SendFromBuffer(buffer);
			else if (direct)
				TryDirect();
			else
				TryData();
		} catch (...) {
			DestroyError(std::current_exception());
		}
	}

	void EventCallback() noexcept {
		TryRead();
	}

	/* virtual methods from class Istream */

	void _SetDirect(FdTypeMask mask) noexcept override {
		direct = (mask & FdTypeMask(fd_type)) != 0;
	}

	off_t _GetAvailable(bool partial) noexcept override;

	void _Read() noexcept override {
		retry_event.Cancel();
		TryRead();
	}

	void _ConsumeDirect(std::size_t) noexcept override {
	}

	void _Close() noexcept override {
		Destroy();
	}
};

inline void
FdIstream::TryData()
{
	if (buffer.IsNull()) {
		buffer.Allocate(fb_pool_get());
	} else {
		const std::size_t available = buffer.GetAvailable();
		if (available > 0) {
			if (SendFromBuffer(buffer) == 0)
				/* not a single byte was consumed: we may have been
				   closed, and we must bail out now */
				return;
		}
	}

	ssize_t nbytes = ReadToBuffer(fd, buffer, INT_MAX);
	if (nbytes == 0) {
		fd.Close();
		if (buffer.empty())
			DestroyEof();
		return;
	} else if (nbytes == -1) {
		throw FmtErrno("Failed to read from '{}'", path);
	}

	assert(!buffer.empty());

	SendFromBuffer(buffer);
}

inline void
FdIstream::TryDirect()
{
	/* first consume the rest of the buffer */
	if (ConsumeFromBuffer(buffer) > 0)
		return;

	switch (InvokeDirect(fd_type, fd, IstreamHandler::NO_OFFSET,
			     INT_MAX, false)) {
	case IstreamDirectResult::CLOSED:
	case IstreamDirectResult::OK:
	case IstreamDirectResult::ASYNC:
	case IstreamDirectResult::BLOCKING:
		break;

	case IstreamDirectResult::END:
		DestroyEof();
		break;

	case IstreamDirectResult::ERRNO:
		if (errno == EAGAIN) {
			/* this should only happen for
			   splice(SPLICE_F_NONBLOCK) from NFS files -
			   unfortunately we cannot use
			   SocketEvent::READ here, so we just install
			   a timer which retries after 100ms */

			retry_event.Schedule(file_retry_timeout);
		} else {
			/* XXX */
			throw FmtErrno("Failed to read from '{}'", path);
		}

		break;
	}
}

/*
 * istream implementation
 *
 */

off_t
FdIstream::_GetAvailable(bool partial) noexcept
{
	return partial
		? buffer.GetAvailable()
		: -1;
}

/*
 * constructor and public methods
 *
 */

UnusedIstreamPtr
NewFdIstream(EventLoop &event_loop, struct pool &pool,
	     const char *path,
	     UniqueFileDescriptor fd, FdType fd_type) noexcept
{
	assert(fd.IsDefined());

	return NewIstreamPtr<FdIstream>(pool, event_loop,
					std::move(fd), fd_type, path);
}
