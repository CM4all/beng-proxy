// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "FileIstream.hxx"
#include "istream.hxx"
#include "New.hxx"
#include "Result.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "lib/fmt/SystemError.hxx"
#include "io/Buffered.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "pool/pool.hxx"
#include "memory/fb_pool.hxx"
#include "memory/SliceFifoBuffer.hxx"
#include "event/FineTimerEvent.hxx"

#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

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
	FineTimerEvent retry_event;

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

	[[gnu::pure]]
	off_t GetRemaining() const noexcept {
		return end_offset - offset;
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
	off_t _Skip(off_t length) noexcept override;

	void _Read() noexcept override {
		retry_event.Cancel();
		TryRead();
	}

	void _ConsumeDirect(std::size_t nbytes) noexcept override;

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

	if (GetRemaining() < off_t(w.size()))
		w = w.first(GetRemaining());

	ssize_t nbytes = pread(fd.Get(), w.data(), w.size(), offset);
	if (nbytes == 0) {
		throw FmtRuntimeError("premature end of file in '{}'", path);
	} else if (nbytes == -1) {
		throw FmtErrno("Failed to read from '{}'", path);
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

	const auto [max_size, then_eof] = CalcMaxDirect(GetRemaining());
	switch (InvokeDirect(FdType::FD_FILE, fd, offset, max_size, then_eof)) {
	case IstreamDirectResult::CLOSED:
	case IstreamDirectResult::BLOCKING:
		break;

	case IstreamDirectResult::OK:
		if (offset >= end_offset)
			EofDetected();
		break;

	case IstreamDirectResult::END:
		throw FmtRuntimeError("premature end of file in '{}'", path);

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
FileIstream::_GetAvailable(bool) noexcept
{
	return GetRemaining() + buffer.GetAvailable();
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

	if (length >= GetRemaining()) {
		/* skip beyond EOF */

		length = GetRemaining();
		offset = end_offset;
	} else {
		/* seek the file descriptor */

		offset += length;
	}

	off_t result = buffer_available + length;
	Consumed(result);
	return result;
}

void
FileIstream::_ConsumeDirect(std::size_t nbytes) noexcept
{
	offset += nbytes;
}

int
FileIstream::_AsFd() noexcept
{
	/* allow this method only if the file descriptor points to a
	   regular file and the specified end offset is the end of the
	   file */
	struct stat st;
	if (fstat(fd.Get(), &st) < 0 || !S_ISREG(st.st_mode) ||
	    end_offset != st.st_size ||
	    /* seek to the current offset (this class doesn't move the
	       file pointer) */
	    lseek(fd.Get(), offset, SEEK_SET) != offset)
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
