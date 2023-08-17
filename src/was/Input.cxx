// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Input.hxx"
#include "was/async/Error.hxx"
#include "event/PipeEvent.hxx"
#include "event/DeferEvent.hxx"
#include "istream/istream.hxx"
#include "istream/Handler.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/Result.hxx"
#include "istream/Bucket.hxx"
#include "pool/pool.hxx"
#include "memory/fb_pool.hxx"
#include "memory/SliceFifoBuffer.hxx"
#include "io/Buffered.hxx"
#include "io/SpliceSupport.hxx"
#include "system/Error.hxx"
#include "util/Compiler.h"
#include "util/ScopeExit.hxx"
#include "util/Exception.hxx"

#include <errno.h>
#include <string.h>
#include <unistd.h>

class WasInput final : Istream {
	PipeEvent event;
	DeferEvent defer_read;

	WasInputHandler &handler;

	SliceFifoBuffer buffer;

	uint64_t received = 0, length;

	bool direct = false;

	bool enabled = false;

	bool closed = false, known_length = false;

public:
	WasInput(struct pool &p, EventLoop &event_loop, FileDescriptor fd,
		 WasInputHandler &_handler) noexcept
		:Istream(p),
		 event(event_loop, BIND_THIS_METHOD(EventCallback), fd),
		 defer_read(event_loop, BIND_THIS_METHOD(OnDeferredRead)),
		 handler(_handler) {
	}

	void Free(std::exception_ptr ep) noexcept;

	using Istream::Destroy;

	void DestroyUnused() noexcept {
		assert(!HasHandler());
		assert(!closed);
		assert(!buffer.IsDefined());

		Destroy();
	}

	UnusedIstreamPtr Enable() noexcept {
		assert(!enabled);
		enabled = true;
		defer_read.Schedule();
		return UnusedIstreamPtr(this);
	}

	void Disable() noexcept {
		event.Cancel();
		defer_read.Cancel();
	}

	bool SetLength(uint64_t _length) noexcept;
	void PrematureThrow(uint64_t _length);
	void Premature(uint64_t _length) noexcept;

private:
	bool HasPipe() const noexcept {
		return event.IsDefined();
	}

	FileDescriptor GetPipe() noexcept {
		return event.GetFileDescriptor();
	}

	bool CanRelease() const {
		return known_length && received == length;
	}

	/**
	 * @return false if the #WasInput has been destroyed
	 */
	bool ReleasePipe() noexcept {
		assert(HasPipe());

		defer_read.Cancel();
		event.Cancel();
		event.ReleaseFileDescriptor();

		return handler.WasInputRelease();
	}

	/**
	 * @return false if the #WasInput has been destroyed
	 */
	bool CheckReleasePipe() noexcept {
		return !CanRelease() || ReleasePipe();
	}

	void ScheduleRead() noexcept {
		assert(HasPipe());
		assert(!buffer.IsDefined() || !buffer.IsFull());

		event.ScheduleRead();
	}

	void CancelRead() noexcept {
		defer_read.Cancel();
		event.CancelRead();
	}

	void AbortError(std::exception_ptr ep) noexcept {
		buffer.FreeIfDefined();
		event.Cancel();

		/* protect against recursive Free() call within the istream
		   handler */
		closed = true;

		handler.WasInputError();
		DestroyError(ep);
	}

	void AbortError(const char *msg) noexcept {
		AbortError(std::make_exception_ptr(WasProtocolError(msg)));
	}

	void Eof() noexcept {
		assert(known_length);
		assert(received == length);
		assert(!buffer.IsDefined());

		event.Cancel();

		handler.WasInputEof();
		DestroyEof();
	}

	bool CheckEof() noexcept {
		if (CanRelease() && buffer.empty()) {
			Eof();
			return true;
		} else
			return false;
	}

	/**
	 * Consume data from the input buffer.
	 *
	 * @return false if the handler blocks or if this object has been
	 * destroyed
	 */
	bool SubmitBuffer() noexcept {
		auto r = buffer.Read();
		if (!r.empty()) {
			std::size_t nbytes = InvokeData(r);
			if (nbytes == 0)
				return false;

			buffer.Consume(nbytes);
			buffer.FreeIfEmpty();
		}

		if (CheckEof())
			return false;

		return true;
	}

	/*
	 * socket i/o
	 *
	 */

	/**
	 * Throws on error.
	 */
	void ReadToBuffer();

	bool TryBuffered() noexcept;
	bool TryDirect() noexcept;

	void TryRead() noexcept {
		if (direct) {
			if (SubmitBuffer() && buffer.empty())
				TryDirect();
		} else {
			TryBuffered();
		}
	}

	void EventCallback(unsigned events) noexcept;
	void OnDeferredRead() noexcept;

	/* virtual methods from class Istream */

	void _SetDirect(FdTypeMask mask) noexcept override {
		direct = (mask & ISTREAM_TO_PIPE) != 0;
	}

	off_t _GetAvailable(bool partial) noexcept override {
		if (known_length)
			return length - received + buffer.GetAvailable();
		else if (partial)
			return buffer.GetAvailable();
		else
			return -1;
	}

	void _Read() noexcept override {
		if (SubmitBuffer())
			TryRead();
	}

	void _FillBucketList(IstreamBucketList &list) override;
	ConsumeBucketResult _ConsumeBucketList(std::size_t nbytes) noexcept override;
	void _ConsumeDirect(std::size_t nbytes) noexcept override;

	void _Close() noexcept override {
		buffer.FreeIfDefined();
		event.Cancel();

		/* protect against recursive Free() call within the istream
		   handler */
		closed = true;

		if (CanRelease()) {
			/* end-of-file was already reached, but was
			   not yet reported to the IstreamHandler; no
			   need to send STOP */

			if (HasPipe() && !ReleasePipe())
				return;

			handler.WasInputEof();
		} else
			handler.WasInputClose(received);

		Destroy();
	}
};

void
WasInput::ReadToBuffer()
{
	std::size_t max_length = FB_SIZE;
	if (known_length) {
		uint64_t rest = length - received;
		if (rest < (uint64_t)max_length)
			max_length = rest;

		if (max_length == 0)
			/* all the data we need is already in the buffer */
			return;
	}

	buffer.AllocateIfNull(fb_pool_get());

	ssize_t nbytes = ::ReadToBuffer(GetPipe(), buffer, max_length);
	assert(nbytes != -2);

	if (nbytes == 0)
		throw WasProtocolError("server closed the data connection");

	if (nbytes < 0) {
		const int e = errno;

		if (e == EAGAIN) {
			buffer.FreeIfEmpty();
			ScheduleRead();
			return;
		}

		throw MakeErrno(e, "read error on WAS data connection");
	}

	received += nbytes;

	if (buffer.IsFull())
		CancelRead();
}

inline bool
WasInput::TryBuffered() noexcept
{
	if (HasPipe()) {
		try {
			ReadToBuffer();
		} catch (...) {
			AbortError(std::current_exception());
			return false;
		}

		if (!CheckReleasePipe())
			return false;
	}

	if (SubmitBuffer()) {
		assert(!buffer.IsDefinedAndFull());

		if (HasPipe())
			ScheduleRead();
	}

	return true;
}

inline bool
WasInput::TryDirect() noexcept
{
	assert(buffer.empty());
	assert(!buffer.IsDefined());

	std::size_t max_length = 0x1000000;
	bool then_eof = false;
	if (known_length) {
		uint64_t rest = length - received;
		if (rest < (uint64_t)max_length) {
			max_length = rest;
			then_eof = true;
		}
	}

	switch (InvokeDirect(FdType::FD_PIPE, GetPipe(),
			     IstreamHandler::NO_OFFSET,
			     max_length, then_eof)) {
	case IstreamDirectResult::BLOCKING:
		CancelRead();
		return false;

	case IstreamDirectResult::END:
	case IstreamDirectResult::CLOSED:
		return false;

	case IstreamDirectResult::ERRNO:
		if (const int e = errno; e == EAGAIN)
			ScheduleRead();
		else
			AbortError(std::make_exception_ptr(MakeErrno(e,
								     "read error on WAS data connection")));
		return false;

	case IstreamDirectResult::OK:
		if (!CheckReleasePipe())
			return false;

		if (CheckEof())
			return false;

		ScheduleRead();
		return true;
	}

	gcc_unreachable();
}

/*
 * libevent callback
 *
 */

inline void
WasInput::EventCallback(unsigned) noexcept
{
	assert(HasPipe());

	TryRead();
}

inline void
WasInput::OnDeferredRead() noexcept
{
	assert(HasPipe());

	TryRead();
}

/*
 * constructor
 *
 */

WasInput *
was_input_new(struct pool &pool, EventLoop &event_loop, FileDescriptor fd,
	      WasInputHandler &handler) noexcept
{
	assert(fd.IsDefined());

	return NewFromPool<WasInput>(pool, pool, event_loop, fd,
				     handler);
}

inline void
WasInput::Free(std::exception_ptr ep) noexcept
{
	assert(ep || closed || !enabled);

	buffer.FreeIfDefined();

	defer_read.Cancel();
	event.Cancel();

	if (!closed && enabled)
		DestroyError(ep);
}

void
was_input_free(WasInput *input, std::exception_ptr ep) noexcept
{
	input->Free(ep);
}

void
was_input_free_unused(WasInput *input) noexcept
{
	input->DestroyUnused();
}

UnusedIstreamPtr
was_input_enable(WasInput &input) noexcept
{
	return input.Enable();
}

void
was_input_disable(WasInput &input) noexcept
{
	input.Disable();
}

inline bool
WasInput::SetLength(uint64_t _length) noexcept
{
	if (known_length) {
		if (_length == length)
			return true;

		// TODO: don't invoke Istream::DestroyError() if not yet enabled
		AbortError("wrong input length announced");
		return false;
	}

	if (_length < received) {
		/* this length must be bogus, because we already received more than that from the socket */
		AbortError("announced length is too small");
		return false;
	}

	length = _length;
	known_length = true;

	if (!CheckReleasePipe())
		return false;

	if (enabled && CheckEof())
		return false;

	return true;
}

bool
was_input_set_length(WasInput *input, uint64_t length) noexcept
{
	return input->SetLength(length);
}

void
WasInput::PrematureThrow(uint64_t _length)
{
	buffer.FreeIfDefined();
	defer_read.Cancel();
	event.Cancel();

	if (known_length && _length > length)
		throw WasProtocolError("announced premature length is too large");

	if (_length < received)
		throw WasProtocolError("announced premature length is too small");

	uint64_t remaining = _length - received;

	while (remaining > 0) {
		uint8_t discard_buffer[4096];
		std::size_t size = std::min(remaining, uint64_t(sizeof(discard_buffer)));
		ssize_t nbytes = GetPipe().Read(discard_buffer, size);
		if (nbytes < 0)
			throw NestException(std::make_exception_ptr(MakeErrno("Read error")),
					    WasError("read error on WAS data connection"));

		if (nbytes == 0)
			throw WasProtocolError("server closed the WAS data connection");

		remaining -= nbytes;
	}
}

inline void
WasInput::Premature(uint64_t _length) noexcept
{
	try {
		PrematureThrow(_length);
	} catch (...) {
		/* protocol error - notify our WasInputHandler */
		handler.WasInputError();
		DestroyError(std::current_exception());
		return;
	}

	/* recovery was successful */

	/* first, release the pipe (which cannot be released
	   already) */
	if (!ReleasePipe())
		/* WasInputHandler::WasInputRelease() has failed and
		   everything has been cleaned up already */
		return;

	/* pretend this is end-of-file, which will cause the
	   WasInputHandler to allow reusing the connection */
	handler.WasInputEof();

	/* finall let our IstreamHandler know that the stream was
	   closed prematurely */
	DestroyError(std::make_exception_ptr(WasProtocolError("premature end of WAS response")));
}

void
was_input_premature(WasInput *input, uint64_t length) noexcept
{
	input->Premature(length);
}

void
was_input_premature_throw(WasInput *input, uint64_t length)
{
	AtScopeExit(input) { input->Destroy(); };
	input->PrematureThrow(length);
	throw WasProtocolError("premature end of WAS response");
}

void
WasInput::_FillBucketList(IstreamBucketList &list)
{
	auto r = buffer.Read();
	if (r.empty()) {
		if (!HasPipe())
			return;

		if (direct) {
			/* prefer splice() over buckets if possible */
			if (!known_length || received < length)
				list.EnableFallback();
			return;
		}

		try {
			ReadToBuffer();
		} catch (...) {
			handler.WasInputError();
			Destroy();
			throw;
		}

		if (!CheckReleasePipe()) {
			// TODO: deal with this condition properly or improve error message
			handler.WasInputError();
			Destroy();
			throw std::runtime_error("WAS peer failed");
		}

		r = buffer.Read();
		if (r.empty()) {
			list.SetMore();
			return;
		}
	}

	list.Push(r);

	if (!known_length || received < length)
		list.SetMore();
}

Istream::ConsumeBucketResult
WasInput::_ConsumeBucketList(std::size_t nbytes) noexcept
{
	std::size_t consumed = std::min(buffer.GetAvailable(), nbytes);

	buffer.Consume(consumed);
	buffer.FreeIfEmpty();

	if (nbytes > 0 && HasPipe())
		ScheduleRead();

	return {Consumed(consumed), buffer.empty() && CanRelease()};
}

void
WasInput::_ConsumeDirect(std::size_t nbytes) noexcept
{
	received += nbytes;
}
