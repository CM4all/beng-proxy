// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "UringIstream.hxx"
#include "istream.hxx"
#include "Bucket.hxx"
#include "New.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "lib/fmt/SystemError.hxx"
#include "io/FileDescriptor.hxx"
#include "io/uring/Operation.hxx"
#include "io/uring/Queue.hxx"
#include "memory/fb_pool.hxx"
#include "memory/SliceFifoBuffer.hxx"
#include "util/SharedLease.hxx"

#include <assert.h>
#include <limits.h>
#include <sys/stat.h>

class UringIstream final : public Istream {
	struct ReadOperation final : Uring::Operation {
		UringIstream &parent;
		Uring::Queue &queue;

		const SharedLease fd_lease;

		SliceFifoBuffer buffer;

		bool released = false;

		ReadOperation(UringIstream &_parent, Uring::Queue &_queue,
			      SharedLease &&_fd_lease) noexcept
			:parent(_parent), queue(_queue),
			 fd_lease(std::move(_fd_lease)) {}

		void Release() noexcept;

		void Start(FileDescriptor file_fd, std::size_t max_read, off_t file_offset);

		/* virtual methods from class Uring::Operation */
		void OnUringCompletion(int res) noexcept override;
	};

	ReadOperation *read_operation;

	/**
	 * The path name.  Only used for error messages.
	 */
	const char *const path;

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
		:Istream(p),
		read_operation(new ReadOperation(*this, _uring, std::move(_lease))),
		 path(_path),
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

	void OnReadError(int error) noexcept;
	void OnReadPrematureEnd() noexcept;
	void OnReadSuccess(std::size_t nbytes) noexcept;

	/* virtual methods from class Istream */

	void _SetDirect(FdTypeMask mask) noexcept override {
		direct = (mask & FdTypeMask(FdType::FD_FILE)) != 0;
	}

	IstreamLength _GetLength() noexcept override;
	off_t _Skip(off_t length) noexcept override;
	void _Read() noexcept override;
	void _ConsumeDirect(std::size_t nbytes) noexcept override;

	void _FillBucketList(IstreamBucketList &list) noexcept override;
	ConsumeBucketResult _ConsumeBucketList(std::size_t nbytes) noexcept override;

	void _Close() noexcept override {
		Destroy();
	}
};

inline void
UringIstream::ReadOperation::Release() noexcept
{
	assert(!released);

	if (IsUringPending()) {
		/* the operation is still pending, and we must not
		   release the buffer yet, or the kernel will later
		   write into this buffer which then belongs somebody
		   else */

		if (auto *s = queue.GetSubmitEntry()) {
			io_uring_prep_cancel(s, GetUringData(), 0);
			io_uring_sqe_set_data(s, nullptr);
			io_uring_sqe_set_flags(s, IOSQE_CQE_SKIP_SUCCESS);
			queue.Submit();
		}

		released = true;
	} else
		delete this;
}

UringIstream::~UringIstream() noexcept
{
	read_operation->Release();
}

inline void
UringIstream::TryDirect() noexcept
try {
	assert(read_operation->buffer.empty());
	assert(!read_operation->IsUringPending());

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
			read_operation->fd_lease.SetBroken();
			throw FmtErrno("Failed to read from '{}'", path);
		}

		break;
	}
} catch (...) {
	DestroyError(std::current_exception());
}

inline void
UringIstream::ReadOperation::Start(FileDescriptor file_fd,
				   std::size_t max_read, off_t file_offset)
{
	assert(!IsUringPending());
	assert(!buffer.IsDefinedAndFull());

	if (buffer.IsNull())
		buffer.Allocate(fb_pool_get());

	auto &s = queue.RequireSubmitEntry();

	auto w = buffer.Write();
	assert(!w.empty());
	if (w.size() > max_read)
		w = w.first(max_read);

	io_uring_prep_read(&s, file_fd.Get(), w.data(), w.size(), file_offset);

	queue.Push(s, *this);
}

void
UringIstream::StartRead() noexcept
{
	size_t max_read = GetMaxRead();
	if (max_read == 0) {
		if (read_operation->buffer.empty())
			DestroyEof();

		return;
	}

	read_operation->Start(fd, max_read, offset);
}

inline void
UringIstream::OnReadError(int error) noexcept
{
	DestroyError(std::make_exception_ptr(FmtErrno(error, "Failed to read from '{}'", path)));
}

inline void
UringIstream::OnReadPrematureEnd() noexcept
{
	DestroyError(std::make_exception_ptr(FmtRuntimeError("Premature end of file in '{}'", path)));
}

inline void
UringIstream::OnReadSuccess(std::size_t nbytes) noexcept
{
	offset += nbytes;

	switch (InvokeReady()) {
	case IstreamReadyResult::OK:
	case IstreamReadyResult::CLOSED:
		return;

	case IstreamReadyResult::FALLBACK:
		break;
	}

	// TODO free the buffer?
	if (SendFromBuffer(read_operation->buffer) > 0 && !read_operation->IsUringPending())
		StartRead();
}

void
UringIstream::ReadOperation::OnUringCompletion(int res) noexcept
{
	if (released) {
		delete this;
		return;
	}

	if (res < 0) {
		fd_lease.SetBroken();
		parent.OnReadError(-res);
		return;
	}

	if (res == 0) {
		parent.OnReadPrematureEnd();
		return;
	}

	buffer.Append(res);
	parent.OnReadSuccess(res);
}

IstreamLength
UringIstream::_GetLength() noexcept
{
	return {
		.length = GetRemaining() + static_cast<off_t>(read_operation->buffer.GetAvailable()),
		.exhaustive = true,
	};
}

off_t
UringIstream::_Skip(off_t length) noexcept
{
	if (length == 0)
		return 0;

	auto &buffer = read_operation->buffer;

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
	const auto &buffer = read_operation->buffer;

	if (auto r = buffer.Read(); !r.empty())
		list.Push(r);

	if (offset < end_offset) {
		list.SetMore();

		if (direct)
			/* the caller prefers sendfile(), so let him
			   invoke Istream::Read() */
			list.EnableFallback();
		else if (buffer.empty() && !read_operation->IsUringPending())
			/* we have no data and there is no pending
			   operation; make sure we have some data next
			   time */
			StartRead();
	}
}

Istream::ConsumeBucketResult
UringIstream::_ConsumeBucketList(std::size_t nbytes) noexcept
{
	auto &buffer = read_operation->buffer;

	bool is_eof = false;
	if (const auto available = buffer.GetAvailable(); nbytes >= available) {
		nbytes = available;
		is_eof = offset == end_offset;
	}

	buffer.Consume(nbytes);

	if (!is_eof && nbytes > 0 && !direct && !read_operation->IsUringPending())
		/* read more data from the file */
		StartRead();

	return {Consumed(nbytes), is_eof};
}

void
UringIstream::_Read() noexcept
{
	// TODO free the buffer?
	if (ConsumeFromBuffer(read_operation->buffer) == 0 && !read_operation->IsUringPending()) {
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
