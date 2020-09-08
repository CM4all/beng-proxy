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

#include "sink_rubber.hxx"
#include "istream/Sink.hxx"
#include "istream/UnusedPtr.hxx"
#include "rubber.hxx"
#include "pool/pool.hxx"
#include "pool/LeakDetector.hxx"
#include "util/Cancellable.hxx"

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

class RubberSink final : IstreamSink, Cancellable, PoolLeakDetector {
	RubberAllocation allocation;

	const size_t max_size;
	size_t position = 0;

	RubberSinkHandler &handler;

public:
	template<typename I>
	RubberSink(struct pool &_pool, RubberAllocation &&_a, size_t _max_size,
		   RubberSinkHandler &_handler,
		   I &&_input,
		   CancellablePointer &cancel_ptr) noexcept
		:IstreamSink(std::forward<I>(_input)),
		 PoolLeakDetector(_pool),
		 allocation(std::move(_a)),
		 max_size(_max_size),
		 handler(_handler)
	{
		input.SetDirect(FD_ANY);
		cancel_ptr = *this;
	}

	void Read() noexcept {
		input.Read();
	}

private:
	void Destroy() noexcept {
		this->~RubberSink();
	}

	void FailTooLarge() noexcept;
	void DestroyEof() noexcept;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override;

	/* virtual methods from class IstreamHandler */
	size_t OnData(const void *data, size_t length) noexcept override;
	ssize_t OnDirect(FdType type, int fd, size_t max_length) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;
};

static ssize_t
fd_read(FdType type, int fd, void *p, size_t size) noexcept
{
	return IsAnySocket(type)
		? recv(fd, p, size, MSG_DONTWAIT)
		: read(fd, p, size);
}

void
RubberSink::FailTooLarge() noexcept
{
	allocation = {};

	if (input.IsDefined())
		input.ClearAndClose();

	auto &_handler = handler;
	Destroy();
	_handler.RubberTooLarge();
}

void
RubberSink::DestroyEof() noexcept
{
	if (input.IsDefined())
		input.ClearAndClose();

	if (position == 0) {
		/* the stream was empty; remove the object from the rubber
		   allocator */
		allocation = {};
	} else
		allocation.Shrink(position);

	auto &_handler = handler;
	auto _allocation = std::move(allocation);
	auto _position = position;
	Destroy();
	_handler.RubberDone(std::move(_allocation), _position);
}

/*
 * istream handler
 *
 */

size_t
RubberSink::OnData(const void *data, size_t length) noexcept
{
	assert(position <= max_size);

	if (position + length > max_size) {
		/* too large, abort and invoke handler */

		FailTooLarge();
		return 0;
	}

	uint8_t *p = (uint8_t *)allocation.Write();
	memcpy(p + position, data, length);
	position += length;

	return length;
}

ssize_t
RubberSink::OnDirect(FdType type, int fd, size_t max_length) noexcept
{
	assert(position <= max_size);

	size_t length = max_size - position;
	if (length == 0) {
		/* already full, see what the file descriptor says */

		uint8_t dummy;
		ssize_t nbytes = fd_read(type, fd, &dummy, sizeof(dummy));
		if (nbytes > 0) {
			FailTooLarge();
			return ISTREAM_RESULT_CLOSED;
		}

		if (nbytes == 0) {
			DestroyEof();
			return ISTREAM_RESULT_CLOSED;
		}

		return ISTREAM_RESULT_ERRNO;
	}

	if (length > max_length)
		length = max_length;

	uint8_t *p = (uint8_t *)allocation.Write();
	p += position;

	ssize_t nbytes = fd_read(type, fd, p, length);
	if (nbytes > 0)
		position += (size_t)nbytes;

	return nbytes;
}

void
RubberSink::OnEof() noexcept
{
	assert(input.IsDefined());
	input.Clear();

	DestroyEof();
}

void
RubberSink::OnError(std::exception_ptr ep) noexcept
{
	assert(input.IsDefined());
	input.Clear();

	auto &_handler = handler;
	Destroy();
	_handler.RubberError(ep);
}

/*
 * async operation
 *
 */

void
RubberSink::Cancel() noexcept
{
	IstreamPointer i(std::move(input));

	Destroy();

	if (i.IsDefined())
		i.ClearAndClose();
}

/*
 * constructor
 *
 */

RubberSink *
sink_rubber_new(struct pool &pool, UnusedIstreamPtr input,
		Rubber &rubber, size_t max_size,
		RubberSinkHandler &handler,
		CancellablePointer &cancel_ptr) noexcept
{
	const off_t available = input.GetAvailable(true);
	if (available > (off_t)max_size) {
		input.Clear();
		handler.RubberTooLarge();
		return nullptr;
	}

	const off_t size = input.GetAvailable(false);
	assert(size == -1 || size >= available);
	assert(size <= (off_t)max_size);
	if (size == 0) {
		input.Clear();
		handler.RubberDone({}, 0);
		return nullptr;
	}

	const size_t allocate = size == -1
		? max_size
		: (size_t)size;

	unsigned rubber_id = rubber.Add(allocate);
	if (rubber_id == 0) {
		input.Clear();
		handler.RubberOutOfMemory();
		return nullptr;
	}

	return NewFromPool<RubberSink>(pool, pool,
				       RubberAllocation(rubber, rubber_id),
				       allocate,
				       handler,
				       std::move(input), cancel_ptr);
}

void
sink_rubber_read(RubberSink &sink) noexcept
{
	sink.Read();
}
