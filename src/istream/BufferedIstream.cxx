// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "BufferedIstream.hxx"
#include "SliceIstream.hxx"
#include "Sink.hxx"
#include "New.hxx"
#include "ConcatIstream.hxx"
#include "PipeLeaseIstream.hxx"
#include "memory/fb_pool.hxx"
#include "memory/SlicePool.hxx"
#include "memory/SliceBuffer.hxx"
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
	std::size_t in_pipe = 0;

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
		pipe.Release(in_pipe == 0
			     ? PutAction::REUSE
			     : PutAction::DESTROY);
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

	IstreamDirectResult ReadToBuffer(FileDescriptor fd,
					 off_t offset,
					 std::size_t max_length) noexcept;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		Destroy();
	}

	/* virtual methods from class IstreamHandler */
	std::size_t OnData(std::span<const std::byte> src) noexcept override;
	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset, std::size_t max_length,
				     bool then_eof) noexcept override;
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
		   ? NewConcatIstream(GetPool(), std::move(i), input.Steal())
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

inline IstreamDirectResult
BufferedIstream::ReadToBuffer(FileDescriptor fd, off_t offset,
			      std::size_t max_length) noexcept
{
	if (!buffer.IsDefined())
		buffer = fb_pool_get().Alloc();

	auto w = buffer.Write();
	if (w.empty())
		/* buffer is full - the "ready" call is pending */
		return IstreamDirectResult::BLOCKING;

	const std::size_t buffer_space = w.size();

	if (w.size() > max_length)
		w = w.first(max_length);

	ssize_t nbytes = HasOffset(offset)
		? fd.ReadAt(offset, w)
		: fd.Read(w);
	if (nbytes <= 0)
		return nbytes < 0
			? IstreamDirectResult::ERRNO
			: IstreamDirectResult::END;

	input.ConsumeDirect(nbytes);
	buffer.Append(nbytes);

	if ((std::size_t)nbytes == buffer_space)
		/* buffer has become full - we can report to handler */
		defer_ready.Schedule();

	return IstreamDirectResult::OK;
}

std::size_t
BufferedIstream::OnData(std::span<const std::byte> src) noexcept
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

	if (w.size() < src.size())
		src = src.first(w.size());

	std::copy(src.begin(), src.end(), w.begin());
	buffer.Append(src.size());

	if (src.size() == w.size())
		/* buffer has become full - we can report to handler */
		defer_ready.Schedule();

	return src.size();
}

IstreamDirectResult
BufferedIstream::OnDirect(FdType type, FileDescriptor fd, off_t offset,
			  std::size_t max_length,
			  [[maybe_unused]] bool then_eof) noexcept
{
	if (buffer.IsDefined() || (type & ISTREAM_TO_PIPE) == 0)
		/* if we have already read something into the buffer,
		   we must continue to do so, even if we suddenly get
		   a file descriptor, because this class is incapable
		   of mixing both */
		return ReadToBuffer(fd, offset, max_length);

	if (!pipe.IsDefined()) {
		/* create the pipe */
		try {
			pipe.Create();
		} catch (...) {
			InvokeError(std::current_exception());
			return IstreamDirectResult::CLOSED;
		}
	}

	defer_ready.Schedule();

	ssize_t nbytes = splice(fd.Get(), ToOffsetPointer(offset),
				pipe.GetWriteFd().Get(), nullptr,
				max_length, SPLICE_F_MOVE|SPLICE_F_NONBLOCK);
	if (nbytes <= 0) {
		if (nbytes == 0)
			return IstreamDirectResult::END;

		const int e = errno;
		if (e == EAGAIN) {
			if (!pipe.GetWriteFd().IsReadyForWriting()) {
				/* the pipe is full - we can report to handler */
				defer_ready.Schedule();
				return IstreamDirectResult::BLOCKING;
			}

			return IstreamDirectResult::ERRNO;
		}

		return IstreamDirectResult::ERRNO;
	}

	in_pipe += nbytes;
	input.ConsumeDirect(nbytes);

	return IstreamDirectResult::OK;
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
