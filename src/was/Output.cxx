/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "Output.hxx"
#include "was/async/Error.hxx"
#include "event/PipeEvent.hxx"
#include "event/DeferEvent.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "io/Iovec.hxx"
#include "io/Splice.hxx"
#include "io/SpliceSupport.hxx"
#include "io/FileDescriptor.hxx"
#include "system/Error.hxx"
#include "istream/Bucket.hxx"
#include "istream/Sink.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "pool/LeakDetector.hxx"
#include "util/DestructObserver.hxx"
#include "util/StaticArray.hxx"

#include <was/protocol.h>

#include <sys/uio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

static constexpr Event::Duration was_output_timeout = std::chrono::minutes(2);

class WasOutput final : PoolLeakDetector, IstreamSink, DestructAnchor {
	PipeEvent event;
	DeferEvent defer_write;
	CoarseTimerEvent timeout_event;

	WasOutputHandler &handler;

	uint64_t sent = 0;

	uint64_t total_length;

	bool known_length = false;

	bool got_data;

public:
	WasOutput(struct pool &pool, EventLoop &event_loop, FileDescriptor fd,
		  UnusedIstreamPtr _input,
		  WasOutputHandler &_handler) noexcept
		:PoolLeakDetector(pool),
		 IstreamSink(std::move(_input)),
		 event(event_loop, BIND_THIS_METHOD(WriteEventCallback), fd),
		 defer_write(event_loop, BIND_THIS_METHOD(OnDeferredWrite)),
		 timeout_event(event_loop, BIND_THIS_METHOD(OnTimeout)),
		 handler(_handler)
	{
		input.SetDirect(ISTREAM_TO_PIPE);

		defer_write.Schedule();
	}

	uint64_t Close() noexcept {
		const auto _sent = sent;
		Destroy();
		return _sent;
	}

	bool CheckLength() noexcept;

private:
	bool HasPipe() const noexcept {
		return event.IsDefined();
	}

	FileDescriptor GetPipe() noexcept {
		return event.GetFileDescriptor();
	}

	void Destroy() noexcept {
		this->~WasOutput();
	}

	void DestroyEof() noexcept {
		assert(!HasInput());

		auto &_handler = handler;
		if (!known_length && !_handler.WasOutputLength(sent))
			return;

		Destroy();
		_handler.WasOutputEof();
	}

	void DestroyPremature(std::exception_ptr ep) noexcept {
		const auto _sent = sent;
		auto &_handler = handler;
		Destroy();
		_handler.WasOutputPremature(_sent, ep);
	}

	void DestroyError(std::exception_ptr ep) noexcept {
		auto &_handler = handler;
		Destroy();
		_handler.WasOutputError(ep);
	}

	bool IsEof() const noexcept {
		return known_length && sent == total_length;
	}

	void ScheduleWrite() noexcept {
		event.ScheduleWrite();
		timeout_event.Schedule(was_output_timeout);
	}

	void WriteEventCallback(unsigned events) noexcept;
	void OnDeferredWrite() noexcept;

	void OnTimeout() noexcept {
		DestroyError(std::make_exception_ptr(WasError("send timeout")));
	}

	/* virtual methods from class IstreamHandler */
	bool OnIstreamReady() noexcept override;
	size_t OnData(const void *data, size_t length) noexcept override;
	ssize_t OnDirect(FdType type, int fd, size_t max_length) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;
};

bool
WasOutput::CheckLength() noexcept
{
	if (known_length)
		return true;

	off_t available = input.GetAvailable(false);
	if (available < 0)
		return true;

	known_length = true;
	total_length = sent + available;
	return handler.WasOutputLength(total_length);
}

/*
 * libevent callback
 *
 */

inline void
WasOutput::WriteEventCallback(unsigned) noexcept
{
	assert(HasPipe());
	assert(HasInput());

	timeout_event.Cancel();

	if (!CheckLength())
		return;

	const DestructObserver destructed(*this);
	got_data = false;

	input.Read();

	if (!destructed && !got_data)
		/* the Istream is not ready for reading, so cancel our
		   write event */
		event.CancelWrite();
}

inline void
WasOutput::OnDeferredWrite() noexcept
{
	assert(HasPipe());
	assert(HasInput());

	if (!CheckLength())
		return;

	input.Read();
}

/*
 * istream handler for the request
 *
 */

bool
WasOutput::OnIstreamReady() noexcept
{
	assert(HasPipe());
	assert(HasInput());

	/* collect buckets */

	IstreamBucketList list;

	try {
		input.FillBucketList(list);
	} catch (...) {
		ClearInput();
		DestroyError(std::current_exception());
		return false;
	}

	if (list.IsEmpty() && !list.HasMore()) {
		/* our input has ended */

		CloseInput();
		DestroyEof();
		return false;
	}

	/* convert buckets to struct iovec array */

	StaticArray<struct iovec, 64> v;
	bool more = list.HasMore(), result = false;
	size_t total = 0;

	for (const auto &i : list) {
		if (!i.IsBuffer()) {
			result = true;
			more = true;
			break;
		}

		if (v.full()) {
			more = true;
			break;
		}

		const auto buffer = i.GetBuffer();

		v.append() = MakeIovec(buffer);
		total += buffer.size();
	}

	if (v.empty())
		return true;

	/* write this struct iovec array */

	ssize_t nbytes = writev(GetPipe().Get(), &v.front(), v.size());
	if (nbytes < 0) {
		int e = errno;
		if (e == EAGAIN) {
			ScheduleWrite();
			return false;
		}

		DestroyError(std::make_exception_ptr(MakeErrno("Write to WAS process failed")));
		return false;
	}

	sent += nbytes;
	input.ConsumeBucketList(nbytes);

	if (!more && size_t(nbytes) == total) {
		/* we've just reached end of our input */

		CloseInput();
		DestroyEof();
		return false;
	}

	ScheduleWrite();
	return result;
}

inline size_t
WasOutput::OnData(const void *p, size_t length) noexcept
{
	assert(HasPipe());
	assert(HasInput());
	assert(!IsEof());

	got_data = true;

	ssize_t nbytes = GetPipe().Write(p, length);
	if (gcc_likely(nbytes > 0)) {
		sent += nbytes;

		if (IsEof()) {
			CloseInput();
			DestroyEof();
			return 0;
		}

		ScheduleWrite();
	} else if (nbytes < 0) {
		if (errno == EAGAIN) {
			ScheduleWrite();
			return 0;
		}

		DestroyError(std::make_exception_ptr(MakeErrno("Write to WAS process failed")));
		return 0;
	}

	return (size_t)nbytes;
}

inline ssize_t
WasOutput::OnDirect(gcc_unused FdType type, int source_fd, size_t max_length) noexcept
{
	assert(HasPipe());
	assert(!IsEof());

	ssize_t nbytes = SpliceToPipe(source_fd, GetPipe().Get(), max_length);
	if (gcc_likely(nbytes > 0)) {
		sent += nbytes;

		if (IsEof()) {
			CloseInput();
			DestroyEof();
			return ISTREAM_RESULT_CLOSED;
		}

		ScheduleWrite();
	} else if (nbytes < 0 && errno == EAGAIN) {
		if (!GetPipe().IsReadyForWriting()) {
			got_data = true;
			ScheduleWrite();
			return ISTREAM_RESULT_BLOCKING;
		}

		/* try again, just in case fd has become ready between
		   the first istream_direct_to_pipe() call and
		   fd.IsReadyForWriting() */
		nbytes = SpliceToPipe(source_fd, GetPipe().Get(), max_length);
	}

	if (nbytes > 0)
		got_data = true;

	return nbytes;
}

void
WasOutput::OnEof() noexcept
{
	assert(HasInput());

	ClearInput();
	DestroyEof();
}

void
WasOutput::OnError(std::exception_ptr ep) noexcept
{
	assert(HasInput());

	ClearInput();

	DestroyPremature(ep);
}

/*
 * constructor
 *
 */

WasOutput *
was_output_new(struct pool &pool, EventLoop &event_loop,
	       FileDescriptor fd, UnusedIstreamPtr input,
	       WasOutputHandler &handler) noexcept
{
	assert(fd.IsDefined());

	return NewFromPool<WasOutput>(pool, pool, event_loop, fd,
				      std::move(input), handler);
}

uint64_t
was_output_free(WasOutput *output) noexcept
{
	assert(output != nullptr);

	return output->Close();
}

bool
was_output_check_length(WasOutput &output) noexcept
{
	return output.CheckLength();
}
