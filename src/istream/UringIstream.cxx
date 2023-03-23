// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "UringIstream.hxx"
#include "istream.hxx"
#include "Bucket.hxx"
#include "New.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "lib/fmt/SystemError.hxx"
#include "io/Iovec.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "io/uring/Close.hxx"
#include "io/uring/Operation.hxx"
#include "io/uring/Queue.hxx"
#include "memory/fb_pool.hxx"
#include "memory/SliceFifoBuffer.hxx"

#include <memory>

#include <assert.h>
#include <limits.h>
#include <sys/stat.h>

class CanceledUringIstream final : public Uring::Operation {
	std::unique_ptr<struct iovec> iov;

	SliceFifoBuffer buffer;

public:
	CanceledUringIstream(std::unique_ptr<struct iovec> &&_iov,
			     SliceFifoBuffer &&_buffer) noexcept
		:iov(std::move(_iov)), buffer(std::move(_buffer)) {}

	void OnUringCompletion(int) noexcept override {
		/* ignore the result and delete this object, which
		   will free the buffer */
		delete this;
	}
};

class UringIstream final : public Istream, Uring::Operation {
	Uring::Queue &uring;

	UniqueFileDescriptor fd;

	/**
	 * Passed to the io_uring read operation.
	 *
	 * It is allocated on the heap, because the kernel may access
	 * it if a read is still in flight and this object is
	 * destroyed; ownership will be passed to
	 * #CanceledUringIstream.
	 */
	std::unique_ptr<struct iovec> iov = std::make_unique<struct iovec>();

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

	SliceFifoBuffer buffer;

	/**
	 * The path name.  Only used for error messages.
	 */
	const char *const path;

	bool direct = false;

public:
	UringIstream(struct pool &p, Uring::Queue &_uring,
		     const char *_path, UniqueFileDescriptor &&_fd,
		     off_t _start_offset, off_t _end_offset) noexcept
		:Istream(p), uring(_uring),
		 fd(std::move(_fd)),
		 offset(_start_offset), end_offset(_end_offset),
		 path(_path)
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

	int _AsFd() noexcept override;
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

		auto *c = new CanceledUringIstream(std::move(iov),
						   std::move(buffer));
		ReplaceUring(*c);
	} else
		Uring::Close(&uring, fd.Release());
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

	*iov = MakeIovec(w);
	io_uring_prep_readv(&s, fd.Get(), iov.get(), 1, offset);

	uring.Push(s, *this);
}

void
UringIstream::OnUringCompletion(int res) noexcept
try {
	if (res < 0)
		throw FmtErrno(-res, "Failed to read from '{}'", path);

	if (res == 0)
		throw FmtRuntimeError("Premature end of file in '{}'", path);

	buffer.Append(res);
	offset += res;

	if (!InvokeReady())
		return;

	// TODO free the buffer?
	if (SendFromBuffer(buffer) > 0)
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

	if (offset < end_offset)
		list.SetMore();
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

int
UringIstream::_AsFd() noexcept
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
NewUringIstream(Uring::Queue &uring, struct pool &pool,
		const char *path, UniqueFileDescriptor fd,
		off_t start_offset, off_t end_offset) noexcept
{
	assert(fd.IsDefined());
	assert(start_offset <= end_offset);

	return NewIstreamPtr<UringIstream>(pool, uring, path, std::move(fd),
					   start_offset, end_offset);
}
