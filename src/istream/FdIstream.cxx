/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "FdIstream.hxx"
#include "istream.hxx"
#include "New.hxx"
#include "Result.hxx"
#include "io/Buffered.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "fb_pool.hxx"
#include "SliceFifoBuffer.hxx"
#include "system/Error.hxx"
#include "event/TimerEvent.hxx"

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
	TimerEvent retry_event;

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

	int _AsFd() noexcept override;
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
		const size_t available = buffer.GetAvailable();
		if (available > 0) {
			if (SendFromBuffer(buffer) == 0)
				/* not a single byte was consumed: we may have been
				   closed, and we must bail out now */
				return;
		}
	}

	ssize_t nbytes = read_to_buffer(fd.Get(), buffer, INT_MAX);
	if (nbytes == 0) {
		fd.Close();
		if (buffer.empty())
			DestroyEof();
		return;
	} else if (nbytes == -1) {
		throw FormatErrno("Failed to read from '%s'", path);
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

	ssize_t nbytes = InvokeDirect(fd_type, fd.Get(), INT_MAX);
	if (nbytes == ISTREAM_RESULT_CLOSED)
		/* this stream was closed during the direct() callback */
		return;

	if (nbytes > 0 || nbytes == ISTREAM_RESULT_BLOCKING) {
	} else if (nbytes == ISTREAM_RESULT_EOF) {
		DestroyEof();
	} else if (errno == EAGAIN) {
		/* this should only happen for splice(SPLICE_F_NONBLOCK) from
		   NFS files - unfortunately we cannot use SocketEvent::READ
		   here, so we just install a timer which retries after
		   100ms */

		retry_event.Schedule(file_retry_timeout);
	} else {
		/* XXX */
		throw FormatErrno("Failed to read from '%s'", path);
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

int
FdIstream::_AsFd() noexcept
{
	if (!fd.IsDefined())
		return -1;

	int result_fd = fd.Steal();
	Destroy();
	return result_fd;
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
