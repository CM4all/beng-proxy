// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "UringSpliceIstream.hxx"
#include "istream.hxx"
#include "New.hxx"
#include "pipe/Lease.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "lib/fmt/SystemError.hxx"
#include "event/DeferEvent.hxx"
#include "io/FileDescriptor.hxx"
#include "io/uring/Operation.hxx"
#include "io/uring/Queue.hxx"
#include "util/SharedLease.hxx"

#include <cassert>
#include <tuple>

#include <fcntl.h>
#include <limits.h>

class UringSpliceIstream final : public Istream {
	struct SpliceOperation final : Uring::Operation {
		UringSpliceIstream &parent;
		Uring::Queue &queue;

		PipeLease pipe;

		bool released = false;

		SpliceOperation(UringSpliceIstream &_parent, Uring::Queue &_queue,
				PipeStock *pipe_stock) noexcept
			:parent(_parent), queue(_queue), pipe(pipe_stock) {}

		~SpliceOperation() noexcept {
			pipe.Release(PutAction::DESTROY);
		}

		void Release() noexcept;

		void Start(FileDescriptor file_fd,
			   std::size_t max_splice, off_t file_offset);

		/* virtual methods from class Uring::Operation */
		void OnUringCompletion(int res) noexcept override;
	};

	SpliceOperation *splice_operation;

	/**
	 * This allows the StartRead() call to be called from a "safe"
	 * stack frame.  This is necessary because _ConsumeDirect() is
	 * not allowed to fail.
	 */
	DeferEvent defer_start;

	/**
	 * The path name.  Only used for error messages.
	 */
	const char *const path;

	const SharedLease fd_lease;

	/**
	 * The number of bytes currently in the pipe.  It may be
	 * negative if data from the pipe was consumed before our
	 * io_uring callback was invoked.
	 */
	off_t in_pipe = 0;

	/**
	 * The file offset of the next/pending splice operation.  If
	 * there is data in the pipe, it precedes this offset.
	 */
	off_t offset;

	/**
	 * We'll stop reading at this file offset.  This is usually
	 * the file size (or the end of the requested range).
	 */
	const off_t end_offset;

	/**
	 * The actual file.
	 */
	FileDescriptor fd;

	/**
	 * Has more data from the pipe been consumed by our handler
	 * while the io_uring splice was pending?  This is used to
	 * restart the operation on EAGAIN.
	 */
	bool consumed_while_pending;

#ifndef NDEBUG
	bool direct = false;
#endif

public:
	UringSpliceIstream(struct pool &p, EventLoop &event_loop, Uring::Queue &_uring,
			   PipeStock *_pipe_stock,
			   const char *_path, FileDescriptor _fd, SharedLease &&_lease,
			   off_t _start_offset, off_t _end_offset)
		:Istream(p),
		 splice_operation(new SpliceOperation(*this, _uring, _pipe_stock)),
		 defer_start(event_loop, BIND_THIS_METHOD(OnDeferredStart)),
		 path(_path), fd_lease(std::move(_lease)),
		 offset(_start_offset), end_offset(_end_offset),
		 fd(_fd)
	{
	}

	~UringSpliceIstream() noexcept override;

private:
	/**
	 * Calculates the remaining number of bytes to be read from
	 * the actual file.
	 */
	[[gnu::pure]]
	off_t GetRemaining() const noexcept {
		return end_offset - offset;
	}

	/**
	 * Calculates the remaining number of bytes including the data
	 * that is currently in the pipe.
	 */
	[[gnu::pure]]
	off_t GetRemainingWithPipe() const noexcept {
		return GetRemaining() + in_pipe;
	}

	[[gnu::pure]]
	std::size_t GetMaxRead() const noexcept {
		/* Linux can't splice() more than 2 GB at a time and
		   may return EINVAL if we ask it to transfer more */
		return std::min(GetRemaining(), off_t(INT_MAX));
	}

	/**
	 * Submit the pipe to the #IstreamHandler (or invoke
	 * IstreamHandler::OnEof() if already at end of file).
	 *
	 * @return false if the object was closed
	 */
	bool TryDirect() noexcept;

	/**
	 * Submit a splice() operation to io_uring (or invoke
	 * IstreamHandler::OnEof() if already at end of file).
	 *
	 * @return false if the object was closed
	 */
	bool StartRead() noexcept;

	void DeferStartRead() noexcept {
		assert(!splice_operation->IsUringPending());

		if (defer_start.IsPending())
			return;

		consumed_while_pending = false;
		defer_start.Schedule();
	}

	void OnDeferredStart() noexcept {
		StartRead();
	}

	void OnSpliceError(int error) noexcept;
	void OnSplicePrematureEnd() noexcept;
	void OnSpliceSuccess(std::size_t nbytes) noexcept;

	/* virtual methods from class Istream */

	void _SetDirect([[maybe_unused]] FdTypeMask mask) noexcept override {
#ifndef NDEBUG
		direct = (mask & FdTypeMask(FdType::FD_PIPE)) != 0;
#endif
	}

	off_t _GetAvailable(bool partial) noexcept override;
	//off_t _Skip(off_t length) noexcept override;  TODO implement using splice(/dev/null)
	void _Read() noexcept override;
	void _ConsumeDirect(std::size_t nbytes) noexcept override;

	void _Close() noexcept override {
		Destroy();
	}
};

inline void
UringSpliceIstream::SpliceOperation::Release() noexcept
{
	assert(!released);

	if (IsUringPending()) {
		/* the operation is still pending; delete this object
		   later (after completion) */

		released = true;
	} else
		delete this;
}

UringSpliceIstream::~UringSpliceIstream() noexcept
{
	splice_operation->Release();
}

inline bool
UringSpliceIstream::TryDirect() noexcept
try {
	assert(in_pipe > 0);

	if (offset - in_pipe >= end_offset) {
		DestroyEof();
		return false;
	}

	/* the pipe is empty; wait for OnUringCompletion() */
	if (in_pipe <= 0)
		return true;

	auto &pipe = splice_operation->pipe;
	assert(pipe.IsDefined());

	const auto [max_size, then_eof] = CalcMaxDirect(GetRemainingWithPipe());
	switch (InvokeDirect(FdType::FD_PIPE, pipe.GetReadFd(), -1, max_size, then_eof)) {
	case IstreamDirectResult::CLOSED:
		return false;

	case IstreamDirectResult::BLOCKING:
		return true;

	case IstreamDirectResult::OK:
		if (offset >= end_offset && in_pipe <= 0) {
			assert(in_pipe == 0);

			pipe.Release(PutAction::REUSE);
			DestroyEof();
			return false;
		}

		break;

	case IstreamDirectResult::ASYNC:
		assert(offset < end_offset || in_pipe > 0);
		break;

	case IstreamDirectResult::END:
		throw FmtRuntimeError("premature end of file in '{}'", path);

	case IstreamDirectResult::ERRNO:
		if (const int e = errno; e != EAGAIN) {
			fd_lease.SetBroken();
			throw FmtErrno(e, "Failed to read from '{}'", path);
		}

		break;
	}

	return true;
} catch (...) {
	DestroyError(std::current_exception());
	return false;
}

inline void
UringSpliceIstream::SpliceOperation::Start(FileDescriptor file_fd,
					   std::size_t max_splice, off_t file_offset)
{
	if (!pipe.IsDefined())
		pipe.Create();

	auto &s = queue.RequireSubmitEntry();
	io_uring_prep_splice(&s, file_fd.Get(), file_offset,
			     pipe.GetWriteFd().Get(), -1,
			     max_splice, SPLICE_F_MOVE);
	queue.Push(s, *this);
}

inline bool
UringSpliceIstream::StartRead() noexcept
{
	assert(!splice_operation->IsUringPending());

	size_t max_read = GetMaxRead();
	if (max_read == 0) {
		/* reached the end of the file */

		if (in_pipe == 0) {
			DestroyEof();
			return false;
		}

		/* there's still data in the pipe to be submitted */
		return true;
	}

	try {
		splice_operation->Start(fd, max_read, offset);
	} catch (...) {
		DestroyError(std::current_exception());
		return false;
	}

	return true;
}

inline void
UringSpliceIstream::OnSpliceError(int error) noexcept
{
	fd_lease.SetBroken();
	DestroyError(std::make_exception_ptr(FmtErrno(error, "Failed to read from '{}'", path)));
}

inline void
UringSpliceIstream::OnSplicePrematureEnd() noexcept
{
	DestroyError(std::make_exception_ptr(FmtRuntimeError("Premature end of file in '{}'", path)));
}

inline void
UringSpliceIstream::OnSpliceSuccess(std::size_t nbytes) noexcept
{
	in_pipe += nbytes;
	offset += nbytes;

	assert(in_pipe >= 0);

	if (in_pipe == 0) [[unlikely]] {
		/* the in-flight pipe data has already been consumed
		   before this completion callback was invoked; now
		   that our bookkeeping is up to date, start another
		   io_uring splice operation to refill the pipe (or
		   report end-of-file to our handler) */

		if (offset >= end_offset)
			DestroyEof();
		else
			DeferStartRead();
		return;
	}

	TryDirect();
}

void
UringSpliceIstream::SpliceOperation::OnUringCompletion(int res) noexcept
{
	if (released) {
		delete this;
		return;
	}

	if (res <= 0) [[unlikely]] {
		if (res == 0) [[unlikely]] {
			parent.OnSplicePrematureEnd();
			return;
		}

		if (res == -EAGAIN) {
			/* this can happen if the pipe is full; this
			   is surprising, because io_uring is supposed
			   to handle EAGAIN, but it does not with
			   non-blocking pipes */

			if (parent.consumed_while_pending)
				/* if data was consumed since the
				   io_uring operation was pending (and
				   until this completion callback was
				   invoked), assume that the pipe is
				   no longer full, so try to restart
				   the read */
				parent.DeferStartRead();

			return;
		}

		parent.OnSpliceError(-res);
		return;
	}

	parent.OnSpliceSuccess(static_cast<std::size_t>(res));
}

off_t
UringSpliceIstream::_GetAvailable(bool) noexcept
{
	return GetRemainingWithPipe();
}

void
UringSpliceIstream::_ConsumeDirect(std::size_t nbytes) noexcept
{
	in_pipe -= nbytes;

	/* we trigger the next io_uring read call from here because
           only here we know the pipe is not full */
	if (offset < end_offset) {
		if (splice_operation->IsUringPending()) {
			consumed_while_pending = true;
		} else {
			DeferStartRead();
		}
	} else {
		if (in_pipe == 0) {
			assert(!splice_operation->IsUringPending());
			splice_operation->pipe.Release(PutAction::REUSE);
		}
	}
}

void
UringSpliceIstream::_Read() noexcept
{
	assert(direct);

	if (in_pipe <= 0) {
		if (!splice_operation->IsUringPending()) {
			/* in_pipe can only be negative if we have
			   consumed data before OnUringCompletion()
			   was called to update in_pipe, i.e it must
			   still be pending */
			assert(in_pipe == 0);

			if (offset == end_offset)
				DestroyEof();
			else
				DeferStartRead();
		}

		return;
	}

	TryDirect();
}

/*
 * constructor and public methods
 *
 */

UnusedIstreamPtr
NewUringSpliceIstream(EventLoop &event_loop, Uring::Queue &uring, PipeStock *pipe_stock,
		      struct pool &pool,
		      const char *path, FileDescriptor fd, SharedLease &&lease,
		      off_t start_offset, off_t end_offset) noexcept
{
	assert(fd.IsDefined());
	assert(start_offset <= end_offset);

	return NewIstreamPtr<UringSpliceIstream>(pool, event_loop, uring, pipe_stock,
						 path, fd, std::move(lease),
						 start_offset, end_offset);
}
