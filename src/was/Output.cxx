// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

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
#include "util/StaticVector.hxx"

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
	IstreamReadyResult OnIstreamReady() noexcept override;
	std::size_t OnData(std::span<const std::byte> src) noexcept override;
	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset, std::size_t max_length,
				     bool then_eof) noexcept override;
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

IstreamReadyResult
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
		return IstreamReadyResult::CLOSED;
	}

	if (list.IsEmpty() && !list.HasMore()) {
		/* our input has ended */

		CloseInput();
		DestroyEof();
		return IstreamReadyResult::CLOSED;
	}

	/* convert buckets to struct iovec array */

	StaticVector<struct iovec, 64> v;
	IstreamReadyResult result = IstreamReadyResult::OK;

	for (const auto &i : list) {
		if (!i.IsBuffer()) {
			result = IstreamReadyResult::FALLBACK;
			break;
		}

		if (v.full())
			break;

		const auto buffer = i.GetBuffer();

		v.push_back(MakeIovec(buffer));
	}

	if (v.empty())
		return IstreamReadyResult::OK;

	/* write this struct iovec array */

	ssize_t nbytes = writev(GetPipe().Get(), v.data(), v.size());
	if (nbytes < 0) {
		int e = errno;
		if (e == EAGAIN) {
			ScheduleWrite();
			return IstreamReadyResult::OK;
		}

		DestroyError(std::make_exception_ptr(MakeErrno("Write to WAS process failed")));
		return IstreamReadyResult::CLOSED;
	}

	sent += nbytes;
	const auto eof = input.ConsumeBucketList(nbytes).eof;

	if (eof) {
		/* we've just reached end of our input */

		CloseInput();
		DestroyEof();
		return IstreamReadyResult::CLOSED;
	}

	ScheduleWrite();
	return result;
}

inline std::size_t
WasOutput::OnData(const std::span<const std::byte> src) noexcept
{
	assert(HasPipe());
	assert(HasInput());
	assert(!IsEof());

	got_data = true;

	ssize_t nbytes = GetPipe().Write(src.data(), src.size());
	if (nbytes > 0) [[likely]] {
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

	return (std::size_t)nbytes;
}

IstreamDirectResult
WasOutput::OnDirect(FdType, FileDescriptor source_fd, off_t source_offset,
		    std::size_t max_length, bool then_eof) noexcept
{
	assert(HasPipe());
	assert(!IsEof());

	if (then_eof && !known_length) {
		known_length = true;
		total_length = sent + max_length;
		if (!handler.WasOutputLength(total_length))
			return IstreamDirectResult::CLOSED;
	}

	ssize_t nbytes = SpliceToPipe(source_fd,
				      ToOffsetPointer(source_offset),
				      GetPipe(),
				      max_length);
	if (nbytes < 0 && errno == EAGAIN) {
		if (!GetPipe().IsReadyForWriting()) {
			got_data = true;
			ScheduleWrite();
			return IstreamDirectResult::BLOCKING;
		}

		/* try again, just in case fd has become ready between
		   the first istream_direct_to_pipe() call and
		   fd.IsReadyForWriting() */
		nbytes = SpliceToPipe(source_fd,
				      ToOffsetPointer(source_offset),
				      GetPipe(),
				      max_length);
	}

	if (nbytes <= 0)
		return nbytes < 0
			? IstreamDirectResult::ERRNO
			: IstreamDirectResult::END;

	input.ConsumeDirect(nbytes);
	sent += nbytes;
	got_data = true;

	if (IsEof()) {
		CloseInput();
		DestroyEof();
		return IstreamDirectResult::CLOSED;
	}

	ScheduleWrite();

	return IstreamDirectResult::OK;
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
