// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "FileIstream.hxx"
#include "istream.hxx"
#include "Bucket.hxx"
#include "New.hxx"
#include "Result.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "lib/fmt/SystemError.hxx"
#include "io/Buffered.hxx"
#include "io/FileDescriptor.hxx"
#include "pool/pool.hxx"
#include "memory/fb_pool.hxx"
#include "memory/SliceFifoBuffer.hxx"
#include "event/FineTimerEvent.hxx"
#include "util/SharedLease.hxx"

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
	/**
	 * A timer to retry reading after EAGAIN.
	 */
	FineTimerEvent retry_event;

	SliceFifoBuffer buffer;
	const char *path;

	const SharedLease fd_lease;

	off_t offset;

	const off_t end_offset;

	FileDescriptor fd;

	bool direct = false;

public:
	FileIstream(struct pool &p, EventLoop &event_loop,
		    FileDescriptor _fd, SharedLease &&_lease,
		    off_t _start_offset, off_t _end_offset,
		    const char *_path) noexcept
		:Istream(p),
		 retry_event(event_loop, BIND_THIS_METHOD(EventCallback)),
		 path(_path), fd_lease(std::move(_lease)),
		 offset(_start_offset), end_offset(_end_offset),
		 fd(_fd) {}

private:
	void EofDetected() noexcept {
		assert(fd.IsDefined());

		DestroyEof();
	}

	[[gnu::pure]]
	uint_least64_t GetRemaining() const noexcept {
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

	IstreamLength _GetLength() noexcept override;

	void _Read() noexcept override {
		retry_event.Cancel();
		TryRead();
	}

	void _ConsumeDirect(std::size_t nbytes) noexcept override;

	void _FillBucketList(IstreamBucketList &list) noexcept override;
	ConsumeBucketResult _ConsumeBucketList(std::size_t nbytes) noexcept override;

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

	if (GetRemaining() < w.size())
		w = w.first(GetRemaining());

	ssize_t nbytes = fd.ReadAt(offset, w);
	if (nbytes == 0) {
		throw FmtRuntimeError("premature end of file in '{}'", path);
	} else if (nbytes == -1) {
		fd_lease.SetBroken();
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

	case IstreamDirectResult::ASYNC:
		assert(offset < end_offset);
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
			fd_lease.SetBroken();
			throw FmtErrno("Failed to read from '{}'", path);
		}

		break;
	}
}

/*
 * istream implementation
 *
 */

IstreamLength
FileIstream::_GetLength() noexcept
{
	return {
		.length = GetRemaining() + buffer.GetAvailable(),
		.exhaustive = true,
	};
}

void
FileIstream::_ConsumeDirect(std::size_t nbytes) noexcept
{
	offset += nbytes;
}

void
FileIstream::_FillBucketList(IstreamBucketList &list) noexcept
{
	if (auto r = buffer.Read(); !r.empty())
		list.Push(r);

	if (offset < end_offset)
		list.EnableFallback(); // TODO read from file
}

Istream::ConsumeBucketResult
FileIstream::_ConsumeBucketList(std::size_t nbytes) noexcept
{
	bool is_eof = false;
	if (const auto available = buffer.GetAvailable(); nbytes >= available) {
		nbytes = available;
		is_eof = offset == end_offset;
	}

	buffer.Consume(nbytes);
	return {Consumed(nbytes), is_eof};
}

/*
 * constructor and public methods
 *
 */

UnusedIstreamPtr
istream_file_fd_new(EventLoop &event_loop, struct pool &pool,
		    const char *path, FileDescriptor fd, SharedLease &&lease,
		    off_t start_offset, off_t end_offset) noexcept
{
	assert(fd.IsDefined());
	assert(start_offset <= end_offset);

	return NewIstreamPtr<FileIstream>(pool, event_loop,
					  fd, std::move(lease),
					  start_offset, end_offset, path);
}
