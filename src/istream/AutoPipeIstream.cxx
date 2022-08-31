/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "AutoPipeIstream.hxx"
#include "PipeLease.hxx"
#include "New.hxx"
#include "UnusedPtr.hxx"
#include "ForwardIstream.hxx"
#include "system/Error.hxx"
#include "io/Splice.hxx"
#include "io/SpliceSupport.hxx"

#include <assert.h>
#include <errno.h>
#include <string.h>

class AutoPipeIstream final : public ForwardIstream {
	PipeLease pipe;
	std::size_t piped = 0;

	FdTypeMask direct_mask = 0;

public:
	AutoPipeIstream(struct pool &p, UnusedIstreamPtr _input,
			PipeStock *_pipe_stock) noexcept
		:ForwardIstream(p, std::move(_input)),
		 pipe(_pipe_stock) {}

	/* virtual methods from class Istream */

	void _SetDirect(FdTypeMask mask) noexcept override;
	off_t _GetAvailable(bool partial) noexcept override;
	void _Read() noexcept override;

	void _FillBucketList(IstreamBucketList &list) override {
		if (piped > 0)
			return Istream::_FillBucketList(list);

		try {
			input.FillBucketList(list);
		} catch (...) {
			Destroy();
			throw;
		}
	}

	void _ConsumeDirect(std::size_t nbytes) noexcept override;

	int _AsFd() noexcept override;
	void _Close() noexcept override;

	/* handler */
	std::size_t OnData(std::span<const std::byte> src) noexcept override;
	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset,
				     std::size_t max_length) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;

private:
	void CloseInternal() noexcept;
	void Abort(std::exception_ptr ep) noexcept;
	IstreamDirectResult Consume() noexcept;
};

void
AutoPipeIstream::CloseInternal() noexcept
{
	/* reuse the pipe only if it's empty */
	pipe.Release(piped == 0);
}

void
AutoPipeIstream::Abort(std::exception_ptr ep) noexcept
{
	CloseInternal();

	DestroyError(ep);
}

IstreamDirectResult
AutoPipeIstream::Consume() noexcept
{
	assert(pipe.IsDefined());
	assert(piped > 0);

	auto result = InvokeDirect(FdType::FD_PIPE, pipe.GetReadFd(),
				   IstreamHandler::NO_OFFSET, piped);
	switch (result) {
	case IstreamDirectResult::BLOCKING:
	case IstreamDirectResult::CLOSED:
		/* handler blocks or pipe was closed */
		break;

	case IstreamDirectResult::END:
		/* must not happen */
		assert(false);
		gcc_unreachable();

	case IstreamDirectResult::ERRNO:
		if (errno != EAGAIN) {
			Abort(std::make_exception_ptr(MakeErrno("read from pipe failed")));
			result = IstreamDirectResult::CLOSED;
		}

		break;

	case IstreamDirectResult::OK:
		if (piped == 0 && !input.IsDefined()) {
			/* our input has already reported EOF, and we have been
			   waiting for the pipe buffer to become empty */
			CloseInternal();
			DestroyEof();
			result = IstreamDirectResult::CLOSED;
		}

		break;
	}

	return result;
}


/*
 * istream handler
 *
 */

inline std::size_t
AutoPipeIstream::OnData(std::span<const std::byte> src) noexcept
{
	assert(HasHandler());

	if (piped > 0) {
		const auto result = Consume();
		if (result != IstreamDirectResult::OK)
			return 0;

		if (piped > 0 || !HasHandler())
			return 0;
	}

	assert(piped == 0);

	return InvokeData(src);
}

inline IstreamDirectResult
AutoPipeIstream::OnDirect(FdType type, FileDescriptor fd, off_t offset,
			  std::size_t max_length) noexcept
{
	assert(HasHandler());

	if (piped > 0) {
		const auto result = Consume();
		if (result != IstreamDirectResult::OK)
			return result;

		if (piped > 0)
			/* if the pipe still isn't empty, we can't start reading
			   new input */
			return IstreamDirectResult::BLOCKING;
	}

	if (direct_mask & FdTypeMask(type))
		/* already supported by handler (maybe already a pipe) - no
		   need for wrapping it into a pipe */
		return InvokeDirect(type, fd, offset, max_length);

	assert((type & ISTREAM_TO_PIPE) == type);

	if (!pipe.IsDefined()) {
		try {
			pipe.Create();
		} catch (...) {
			Abort(std::current_exception());
			return IstreamDirectResult::CLOSED;
		}
	}

	ssize_t nbytes = Splice(fd, nullptr,
				pipe.GetWriteFd(), nullptr,
				max_length);
	/* don't check EAGAIN here (and don't return -2).  We assume that
	   splicing to the pipe cannot possibly block, since we flushed
	   the pipe; assume that it can only be the source file which is
	   blocking */
	if (nbytes <= 0)
		return nbytes < 0
			? IstreamDirectResult::ERRNO
			: IstreamDirectResult::END;

	input.ConsumeDirect(nbytes);

	assert(piped == 0);
	piped = (std::size_t)nbytes;

	if (Consume() == IstreamDirectResult::CLOSED)
		return IstreamDirectResult::CLOSED;

	return IstreamDirectResult::OK;
}

inline void
AutoPipeIstream::OnEof() noexcept
{
	input.Clear();

	pipe.CloseWriteIfNotStock();

	if (piped == 0) {
		CloseInternal();
		DestroyEof();
	}
}

inline void
AutoPipeIstream::OnError(std::exception_ptr ep) noexcept
{
	CloseInternal();
	input.Clear();
	DestroyError(ep);
}

/*
 * istream implementation
 *
 */

void
AutoPipeIstream::_SetDirect(FdTypeMask mask) noexcept
{
	direct_mask = mask;

	if (mask & FdType::FD_PIPE)
		/* if the handler supports the pipe, we offer our
		   services */
		mask |= ISTREAM_TO_PIPE;

	input.SetDirect(mask);
}

off_t
AutoPipeIstream::_GetAvailable(bool partial) noexcept
{
	if (gcc_likely(input.IsDefined())) {
		off_t available = input.GetAvailable(partial);
		if (piped > 0) {
			if (available != -1)
				available += piped;
			else if (partial)
				available = piped;
		}

		return available;
	} else {
		assert(piped > 0);

		return piped;
	}
}

void
AutoPipeIstream::_Read() noexcept
{
	if (piped > 0 && (Consume() != IstreamDirectResult::OK || piped > 0))
		return;

	/* at this point, the pipe must be flushed - if the pipe is
	   flushed, this stream is either closed or there must be an input
	   stream */
	assert(input.IsDefined());

	input.Read();
}

void
AutoPipeIstream::_ConsumeDirect(std::size_t nbytes) noexcept
{
	if (piped > 0) {
		assert(nbytes <= piped);
		piped -= nbytes;

		if (piped == 0)
			/* if the pipe was drained, return it to the
			   stock, to make it available to other
			   streams */
			pipe.ReleaseIfStock();
	} else {
		ForwardIstream::_ConsumeDirect(nbytes);
	}
}

int
AutoPipeIstream::_AsFd() noexcept
{
	if (piped > 0)
		/* need to flush the pipe buffer first */
		return -1;

	int fd = input.AsFd();
	if (fd >= 0) {
		CloseInternal();
		Destroy();
	}

	return fd;
}

void
AutoPipeIstream::_Close() noexcept
{
	CloseInternal();

	Destroy();
}

/*
 * constructor
 *
 */

UnusedIstreamPtr
NewAutoPipeIstream(struct pool *pool, UnusedIstreamPtr input,
		   PipeStock *pipe_stock) noexcept
{
	return NewIstreamPtr<AutoPipeIstream>(*pool, std::move(input), pipe_stock);
}
