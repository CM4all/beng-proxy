// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "UringIstream.hxx"
#include "istream.hxx"
#include "Bucket.hxx"
#include "New.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "lib/fmt/SystemError.hxx"
#include "io/SharedFd.hxx"
#include "io/FileDescriptor.hxx"
#include "io/uring/Operation.hxx"
#include "io/uring/Queue.hxx"
#include "memory/fb_pool.hxx"
#include "memory/SliceFifoBuffer.hxx"

#include <assert.h>
#include <limits.h>
#include <sys/stat.h>

class CanceledUringIstream final : public Uring::Operation {
	SliceFifoBuffer buffer;

public:
	explicit CanceledUringIstream(SliceFifoBuffer &&_buffer) noexcept
		:buffer(std::move(_buffer)) {}

	void OnUringCompletion(int) noexcept override {
		/* ignore the result and delete this object, which
		   will free the buffer */
		delete this;
	}
};

class UringIstream final : public Istream, Uring::Operation {
	Uring::Queue &uring;

	SliceFifoBuffer buffer;

	/**
	 * The path name.  Only used for error messages.
	 */
	const char *const path;

	const SharedLease fd_lease;

	/**
	 * The file offset of the next/pending read operation.  If
	 * there is data in the #buffer, it precedes this offset.
	 */
	off_t offset;

	/**
	 * We'll stop reading at this file offset.  This is usually
	 * the file size (or the end of the requested range).
	 */
	const off_t end_offset;

	FileDescriptor fd;

	bool direct = false;

public:
	UringIstream(struct pool &p, Uring::Queue &_uring,
		     const char *_path, FileDescriptor _fd, SharedLease &&_lease,
		     off_t _start_offset, off_t _end_offset) noexcept
		:Istream(p), uring(_uring),
		 path(_path), fd_lease(std::move(_lease)),
		 offset(_start_offset), end_offset(_end_offset),
		 fd(_fd)
	{
	}

	~UringIstream() noexcept override;

private:
	[[gnu::pure]]
	off_t GetRemaining() const noexcept {
		return end_offset - offset;
	}

	[[gnu::pure]]
	size_t GetMaxRead() const noexcept {
		return std::min(GetRemaining(), off_t(INT_MAX));
	}

	void TryDirect() noexcept;

	void StartRead() noexcept;

	/* virtual methods from class Uring::Operation */
	void OnUringCompletion(int res) noexcept override;

	/* virtual methods from class Istream */

	void _SetDirect(FdTypeMask mask) noexcept override {
		direct = (mask & FdTypeMask(FdType::FD_FILE)) != 0;
	}

	off_t _GetAvailable(bool partial) noexcept override;
	off_t _Skip(off_t length) noexcept override;
	void _Read() noexcept override;
	void _ConsumeDirect(std::size_t nbytes) noexcept override;

	void _FillBucketList(IstreamBucketList &list) noexcept override;
	ConsumeBucketResult _ConsumeBucketList(std::size_t nbytes) noexcept override;

	void _Close() noexcept override {
		Destroy();
	}
};

UringIstream::~UringIstream() noexcept
{
	if (IsUringPending()) {
		/* the operation is still pending, and we must not
		   release the buffer yet, or the kernel will later
		   write into this buffer which then belongs somebody
		   else */

		assert(buffer.IsDefined());

		auto *c = new CanceledUringIstream(std::move(buffer));
		ReplaceUring(*c);
	}
}

inline void
UringIstream::TryDirect() noexcept
try {
	assert(buffer.empty());
	assert(!IsUringPending());

	if (offset >= end_offset) {
		DestroyEof();
		return;
	}

	const auto [max_size, then_eof] = CalcMaxDirect(GetRemaining());
	switch (InvokeDirect(FdType::FD_FILE, fd, offset, max_size, then_eof)) {
	case IstreamDirectResult::CLOSED:
	case IstreamDirectResult::BLOCKING:
		break;

	case IstreamDirectResult::OK:
		if (offset >= end_offset)
			DestroyEof();
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
			   fall back to io_uring read() */

			StartRead();
		} else {
			/* XXX */
			fd_lease.SetBroken();
			throw FmtErrno("Failed to read from '{}'", path);
		}

		break;
	}
} catch (...) {
	DestroyError(std::current_exception());
}

void
UringIstream::StartRead() noexcept
{
	assert(!IsUringPending());
	assert(!buffer.IsDefinedAndFull());

	size_t max_read = GetMaxRead();
	if (max_read == 0) {
		if (buffer.empty())
			DestroyEof();

		return;
	}

	if (buffer.IsNull())
		buffer.Allocate(fb_pool_get());

	auto &s = uring.RequireSubmitEntry();

	auto w = buffer.Write();
	assert(!w.empty());
	if (w.size() > GetMaxRead())
		w = w.first(GetMaxRead());

	io_uring_prep_read(&s, fd.Get(), w.data(), w.size(), offset);

	uring.Push(s, *this);
}

void
UringIstream::OnUringCompletion(int res) noexcept
try {
	if (res < 0) {
		fd_lease.SetBroken();
		throw FmtErrno(-res, "Failed to read from '{}'", path);
	}

	if (res == 0)
		throw FmtRuntimeError("Premature end of file in '{}'", path);

	buffer.Append(res);
	offset += res;

	switch (InvokeReady()) {
	case IstreamReadyResult::OK:
	case IstreamReadyResult::CLOSED:
		return;

	case IstreamReadyResult::FALLBACK:
		break;
	}

	// TODO free the buffer?
	if (SendFromBuffer(buffer) > 0 && !IsUringPending())
		StartRead();
} catch (...) {
	DestroyError(std::current_exception());
}

off_t
UringIstream::_GetAvailable(bool) noexcept
{
	return GetRemaining() + buffer.GetAvailable();
}

off_t
UringIstream::_Skip(off_t length) noexcept
{
	if (length == 0)
		return 0;

	const size_t buffer_available = buffer.GetAvailable();
	if (length <= off_t(buffer_available)) {
		buffer.Consume(length);
		Consumed(length);
		return length;
	}

	buffer.Consume(buffer_available);
	Consumed(buffer_available);

	// TODO: skip more data?  what about the pending read?

	return buffer_available;
}

void
UringIstream::_ConsumeDirect(std::size_t nbytes) noexcept
{
	offset += nbytes;
}

void
UringIstream::_FillBucketList(IstreamBucketList &list) noexcept
{
	if (auto r = buffer.Read(); !r.empty())
		list.Push(r);

	if (offset < end_offset) {
		list.SetMore();

		if (direct)
			/* the caller prefers sendfile(), so let him
			   invoke Istream::Read() */
			list.EnableFallback();
		else if (buffer.empty() && !IsUringPending())
			/* we have no data and there is no pending
			   operation; make sure we have some data next
			   time */
			StartRead();
	}
}

Istream::ConsumeBucketResult
UringIstream::_ConsumeBucketList(std::size_t nbytes) noexcept
{
	bool is_eof = false;
	if (const auto available = buffer.GetAvailable(); nbytes >= available) {
		nbytes = available;
		is_eof = offset == end_offset;
	}

	buffer.Consume(nbytes);

	if (!is_eof && nbytes > 0 && !direct && !IsUringPending())
		/* read more data from the file */
		StartRead();

	return {Consumed(nbytes), is_eof};
}

void
UringIstream::_Read() noexcept
{
	// TODO free the buffer?
	if (ConsumeFromBuffer(buffer) == 0 && !IsUringPending()) {
		if (direct)
			TryDirect();
		else
			StartRead();
	}
}

/*
 * constructor and public methods
 *
 */

UnusedIstreamPtr
NewUringIstream(Uring::Queue &uring, struct pool &pool,
		const char *path, FileDescriptor fd, SharedLease &&lease,
		off_t start_offset, off_t end_offset) noexcept
{
	assert(fd.IsDefined());
	assert(start_offset <= end_offset);

	return NewIstreamPtr<UringIstream>(pool, uring, path, fd, std::move(lease),
					   start_offset, end_offset);
}
