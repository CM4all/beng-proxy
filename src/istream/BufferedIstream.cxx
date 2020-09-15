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

#include "BufferedIstream.hxx"
#include "Sink.hxx"
#include "New.hxx"
#include "ConcatIstream.hxx"
#include "SliceIstream.hxx"
#include "PipeLeaseIstream.hxx"
#include "PipeLease.hxx"
#include "fb_pool.hxx"
#include "SlicePool.hxx"
#include "SliceBuffer.hxx"
#include "pool/pool.hxx"
#include "pool/Holder.hxx"
#include "event/DeferEvent.hxx"
#include "io/SpliceSupport.hxx"
#include "util/Cancellable.hxx"

#include <fcntl.h>
#include <errno.h>
#include <string.h>

class BufferedIstream final : PoolHolder, IstreamSink, Cancellable {
	BufferedIstreamHandler &handler;

	/**
	 * A buffer which collects data.
	 *
	 * Note: can't use both #buffer and #pipe because then we wouldn't
	 * know in which order to submit both.
	 */
	SliceBuffer buffer;

	/**
	 * A pipe which collects "direct" data via splice().
	 *
	 * Note: can't use both #buffer and #pipe because then we wouldn't
	 * know in which order to submit both.
	 */
	PipeLease pipe;

	/**
	 * How many bytes were spliced into the pipe?
	 */
	size_t in_pipe = 0;

	/**
	 * This event postpones the
	 * BufferedIstreamHandler::OnBufferedIstreamReady() call to move
	 * it out of the current stack frame.  This is sometimes necessary
	 * because that call will modify our input's handler, which is an
	 * unsafe operation from inside the handler.
	 */
	DeferEvent defer_ready;

public:
	BufferedIstream(struct pool &_p, EventLoop &_event_loop,
			PipeStock *_pipe_stock,
			BufferedIstreamHandler &_handler,
			UnusedIstreamPtr &&_input,
			CancellablePointer &cancel_ptr) noexcept
		:PoolHolder(_p), IstreamSink(std::move(_input)),
		 handler(_handler),
		 pipe(_pipe_stock),
		 defer_ready(_event_loop, BIND_THIS_METHOD(DeferredReady))
	{
		cancel_ptr = *this;

		input.SetDirect(FD_ANY);
	}

	void Start() noexcept {
		input.Read();
	}

private:
	~BufferedIstream() noexcept {
		pipe.Release(in_pipe == 0);
	}

	void Destroy() noexcept {
		this->~BufferedIstream();
	}

	UnusedIstreamPtr CommitBuffered() noexcept;
	UnusedIstreamPtr Commit() noexcept;

	void DeferredReady() noexcept;

	void InvokeError(std::exception_ptr e) noexcept {
		auto &_handler = handler;
		Destroy();
		_handler.OnBufferedIstreamError(std::move(e));
	}

	ssize_t ReadToBuffer(int fd, size_t max_length) noexcept;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		Destroy();
	}

	/* virtual methods from class IstreamHandler */
	size_t OnData(const void *data, size_t length) noexcept override;
	ssize_t OnDirect(FdType type, int fd, size_t max_length) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr e) noexcept override;
};

UnusedIstreamPtr
BufferedIstream::CommitBuffered() noexcept
{
	if (buffer.IsDefined())
		return NewIstreamPtr<SliceIstream>(GetPool(), std::move(buffer));
	else if (in_pipe > 0)
		return NewIstreamPtr<PipeLeaseIstream>(GetPool(), std::move(pipe), in_pipe);
	else
		return nullptr;
}

UnusedIstreamPtr
BufferedIstream::Commit() noexcept
{
	UnusedIstreamPtr i = CommitBuffered();

	return i
		? (HasInput()
		   ? istream_cat_new(GetPool(), std::move(i), input.Steal())
		   : std::move(i))
		: input.Steal();
}

void
BufferedIstream::DeferredReady() noexcept
{
	auto i = Commit();
	auto &_handler = handler;
	Destroy();
	_handler.OnBufferedIstreamReady(std::move(i));
}

inline ssize_t
BufferedIstream::ReadToBuffer(int fd, size_t max_length) noexcept
{
	if (!buffer.IsDefined())
		buffer = fb_pool_get().Alloc();

	const auto w = buffer.Write();
	if (w.empty())
		/* buffer is full - the "ready" call is pending */
		return ISTREAM_RESULT_BLOCKING;

	ssize_t nbytes = read(fd, w.data, std::min(w.size, max_length));
	if (nbytes > 0) {
		buffer.Append(nbytes);

		if ((size_t)nbytes == w.size)
			/* buffer has become full - we can report to handler */
			defer_ready.Schedule();
}

	return nbytes;
}

size_t
BufferedIstream::OnData(const void *data, size_t length) noexcept
{
	if (in_pipe > 0) {
		/* can't fill both buffer and pipe; stop here and report to
		   handler */
		defer_ready.Schedule();
		return 0;
	}

	if (!buffer.IsDefined())
		buffer = fb_pool_get().Alloc();

	auto w = buffer.Write();
	if (w.empty())
		/* buffer is full - the "ready" call is pending */
		return 0;

	size_t nbytes = std::min(length, w.size);
	memcpy(w.data, data, nbytes);
	buffer.Append(nbytes);

	if (nbytes == w.size)
		/* buffer has become full - we can report to handler */
		defer_ready.Schedule();

	return nbytes;
}

ssize_t
BufferedIstream::OnDirect(FdType type, int fd, size_t max_length) noexcept
{
	if (buffer.IsDefined() || (type & ISTREAM_TO_PIPE) == 0)
		/* if we have already read something into the buffer,
		   we must continue to do so, even if we suddenly get
		   a file descriptor, because this class is incapable
		   of mixing both */
		return ReadToBuffer(fd, max_length);

	if (!pipe.IsDefined()) {
		/* create the pipe */
		try {
			pipe.Create();
		} catch (...) {
			InvokeError(std::current_exception());
			return ISTREAM_RESULT_CLOSED;
		}
	}

	defer_ready.Schedule();

	ssize_t nbytes = splice(fd, nullptr, pipe.GetWriteFd().Get(), nullptr,
				max_length, SPLICE_F_MOVE|SPLICE_F_NONBLOCK);
	if (nbytes < 0) {
		const int e = errno;
		if (e == EAGAIN) {
			if (!pipe.GetWriteFd().IsReadyForWriting()) {
				/* the pipe is full - we can report to handler */
				defer_ready.Schedule();
				return ISTREAM_RESULT_BLOCKING;
			}

			return ISTREAM_RESULT_ERRNO;
		}

		return ISTREAM_RESULT_ERRNO;
	}

	in_pipe += nbytes;
	return nbytes;
}

void
BufferedIstream::OnEof() noexcept
{
	ClearInput();
	defer_ready.Schedule();
}

void
BufferedIstream::OnError(std::exception_ptr e) noexcept
{
	ClearInput();
	InvokeError(std::move(e));
}

void
NewBufferedIstream(struct pool &pool, EventLoop &event_loop,
		   PipeStock *pipe_stock,
		   BufferedIstreamHandler &handler,
		   UnusedIstreamPtr i,
		   CancellablePointer &cancel_ptr) noexcept
{
	auto *b = NewFromPool<BufferedIstream>(pool, pool, event_loop, pipe_stock,
					       handler, std::move(i), cancel_ptr);
	b->Start();
}
