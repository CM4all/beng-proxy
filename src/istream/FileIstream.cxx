/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "FileIstream.hxx"
#include "istream.hxx"
#include "New.hxx"
#include "Result.hxx"
#include "io/Buffered.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "pool/pool.hxx"
#include "fb_pool.hxx"
#include "SliceFifoBuffer.hxx"
#include "system/Error.hxx"
#include "event/TimerEvent.hxx"
#include "util/RuntimeError.hxx"

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

class FileIstream final : public Istream {
	UniqueFileDescriptor fd;

	/**
	 * A timer to retry reading after EAGAIN.
	 */
	TimerEvent retry_event;

	off_t offset;

	const off_t end_offset;

	SliceFifoBuffer buffer;
	const char *path;

	bool direct = false;

public:
	FileIstream(struct pool &p, EventLoop &event_loop,
		    UniqueFileDescriptor &&_fd,
		    off_t _start_offset, off_t _end_offset,
		    const char *_path) noexcept
		:Istream(p),
		 fd(std::move(_fd)),
		 retry_event(event_loop, BIND_THIS_METHOD(EventCallback)),
		 offset(_start_offset), end_offset(_end_offset),
		 path(_path) {}

private:
	void EofDetected() noexcept {
		assert(fd.IsDefined());

		DestroyEof();
	}

	gcc_pure
	size_t GetMaxRead() const noexcept {
		return std::min(end_offset - offset, off_t(INT_MAX));
	}

	void TryData();
	void TryDirect();

	void TryRead() noexcept {
		try {
			if (direct)
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
		direct = (mask & FdTypeMask(FdType::FD_FILE)) != 0;
	}

	off_t _GetAvailable(bool partial) noexcept override;
	off_t _Skip(gcc_unused off_t length) noexcept override;

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
FileIstream::TryData()
{
	if (buffer.IsNull()) {
		if (offset >= end_offset) {
			EofDetected();
			return;
		}

		buffer.Allocate(fb_pool_get());
	} else if (!buffer.empty()) {
		if (SendFromBuffer(buffer) == 0)
			/* not a single byte was consumed: we may have
			   been closed, and we must bail out now */
			return;
	}

	if (offset >= end_offset) {
		if (buffer.empty())
			EofDetected();
		return;
	}

	auto w = buffer.Write();
	assert(!w.empty());

	if (end_offset - offset < off_t(w.size))
		w.size = end_offset - offset;

	ssize_t nbytes = pread(fd.Get(), w.data, w.size, offset);
	if (nbytes == 0) {
		throw FormatRuntimeError("premature end of file in '%s'",
					 path);
	} else if (nbytes == -1) {
		throw FormatErrno("Failed to read from '%s'", path);
	} else if (nbytes > 0) {
		buffer.Append(nbytes);
		offset += nbytes;
	}

	assert(!buffer.empty());

	if (ConsumeFromBuffer(buffer) == 0 && offset >= end_offset)
		EofDetected();
}

inline void
FileIstream::TryDirect()
{
	/* first consume the rest of the buffer */
	if (ConsumeFromBuffer(buffer) > 0)
		return;

	if (offset >= end_offset) {
		EofDetected();
		return;
	}

	// TODO: eliminate the lseek() call by passing the offset to
	// InvokeDirect() (requires an IstreamHandler API change)
	if (fd.Seek(offset) < 0)
		throw FormatErrno("Failed to seek '%s'", path);

	ssize_t nbytes = InvokeDirect(FdType::FD_FILE, fd.Get(), GetMaxRead());
	if (nbytes == ISTREAM_RESULT_CLOSED)
		/* this stream was closed during the direct() callback */
		return;

	if (nbytes > 0 || nbytes == ISTREAM_RESULT_BLOCKING) {
		/* -2 means the callback wasn't able to consume any data right
		   now */
		if (nbytes > 0) {
			offset += nbytes;
			if (offset >= end_offset)
				EofDetected();
		}
	} else if (nbytes == ISTREAM_RESULT_EOF) {
		throw FormatRuntimeError("premature end of file in '%s'", path);
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
FileIstream::_GetAvailable(bool) noexcept
{
	return (end_offset - offset) + buffer.GetAvailable();
}

off_t
FileIstream::_Skip(off_t length) noexcept
{
	retry_event.Cancel();

	if (length == 0)
		return 0;

	const size_t buffer_available = buffer.GetAvailable();
	if (length < off_t(buffer_available)) {
		buffer.Consume(length);
		Consumed(length);
		return length;
	}

	length -= buffer_available;
	buffer.Clear();

	if (length >= end_offset - offset) {
		/* skip beyond EOF */

		length = end_offset - offset;
		offset = end_offset;
	} else {
		/* seek the file descriptor */

		offset += length;
	}

	off_t result = buffer_available + length;
	Consumed(result);
	return result;
}

int
FileIstream::_AsFd() noexcept
{
	int result_fd = fd.Steal();

	Destroy();

	return result_fd;
}

/*
 * constructor and public methods
 *
 */

UnusedIstreamPtr
istream_file_fd_new(EventLoop &event_loop, struct pool &pool,
		    const char *path, UniqueFileDescriptor fd,
		    off_t start_offset, off_t end_offset) noexcept
{
	assert(fd.IsDefined());
	assert(start_offset <= end_offset);

	return NewIstreamPtr<FileIstream>(pool, event_loop,
					  std::move(fd),
					  start_offset, end_offset, path);
}
