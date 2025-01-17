// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "UringSpliceIstream.hxx"
#include "istream.hxx"
#include "New.hxx"
#include "pipe/Lease.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "lib/fmt/SystemError.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "io/uring/Close.hxx"
#include "io/uring/Operation.hxx"
#include "io/uring/Queue.hxx"

#include <cassert>
#include <tuple>

#include <fcntl.h>
#include <limits.h>

class UringSpliceIstream final : public Istream, Uring::Operation {
	Uring::Queue &uring;

	/**
	 * The path name.  Only used for error messages.
	 */
	const char *const path;

	/**
	 * The actual file.
	 */
	UniqueFileDescriptor fd;

	PipeLease pipe;

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

#ifndef NDEBUG
	bool direct = false;
#endif

public:
	UringSpliceIstream(struct pool &p, Uring::Queue &_uring,
			   PipeStock *_pipe_stock,
			   const char *_path, UniqueFileDescriptor &&_fd,
			   off_t _start_offset, off_t _end_offset)
		:Istream(p), uring(_uring),
		 path(_path),
		 fd(std::move(_fd)),
		 pipe(_pipe_stock),
		 offset(_start_offset), end_offset(_end_offset)
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

	/* virtual methods from class Uring::Operation */
	void OnUringCompletion(int res) noexcept override;

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

UringSpliceIstream::~UringSpliceIstream() noexcept
{
	Uring::Close(&uring, fd.Release());
	pipe.Release(PutAction::DESTROY);
}

inline bool
UringSpliceIstream::TryDirect() noexcept
try {
	if (offset - in_pipe >= end_offset) {
		DestroyEof();
		return false;
	}

	/* the pipe is empty; wait for OnUringCompletion() */
	if (in_pipe <= 0)
		return true;

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

	case IstreamDirectResult::END:
		throw FmtRuntimeError("premature end of file in '{}'", path);

	case IstreamDirectResult::ERRNO:
		throw FmtErrno("Failed to read from '{}'", path);
	}

	return true;
} catch (...) {
	DestroyError(std::current_exception());
	return false;
}

bool
UringSpliceIstream::StartRead() noexcept
{
	assert(!IsUringPending());

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

	if (!pipe.IsDefined()) {
		try {
			pipe.Create();
		} catch (...) {
			DestroyError(std::current_exception());
			return false;
		}
	}

	auto &s = uring.RequireSubmitEntry();
	io_uring_prep_splice(&s, fd.Get(), offset,
			     pipe.GetWriteFd().Get(), -1,
			     max_read, SPLICE_F_MOVE);
	uring.Push(s, *this);

	return true;
}

void
UringSpliceIstream::OnUringCompletion(int res) noexcept
try {
	if (res <= 0) [[unlikely]] {
		if (res == 0) [[unlikely]]
			throw FmtRuntimeError("Premature end of file in '{}'", path);

		throw FmtErrno(-res, "Failed to read from '{}'", path);
	}

	in_pipe += res;
	offset += res;

	assert(in_pipe >= 0);

	if (StartRead())
		TryDirect();
} catch (...) {
	DestroyError(std::current_exception());
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

	if (!IsUringPending() && in_pipe == 0)
		pipe.Release(PutAction::REUSE);
}

void
UringSpliceIstream::_Read() noexcept
{
	assert(direct);

	if (IsUringPending() || StartRead())
		TryDirect();
}

/*
 * constructor and public methods
 *
 */

UnusedIstreamPtr
NewUringSpliceIstream(Uring::Queue &uring, PipeStock *pipe_stock,
		      struct pool &pool,
		      const char *path, UniqueFileDescriptor fd,
		      off_t start_offset, off_t end_offset) noexcept
{
	assert(fd.IsDefined());
	assert(start_offset <= end_offset);

	return NewIstreamPtr<UringSpliceIstream>(pool, uring, pipe_stock,
						 path, std::move(fd),
						 start_offset, end_offset);
}
