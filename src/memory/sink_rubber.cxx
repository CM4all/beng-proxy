// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "sink_rubber.hxx"
#include "Rubber.hxx"
#include "istream/Sink.hxx"
#include "istream/UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "pool/LeakDetector.hxx"
#include "net/SocketDescriptor.hxx"
#include "util/Cancellable.hxx"

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

class RubberSink final : IstreamSink, Cancellable, PoolLeakDetector {
	RubberAllocation allocation;

	const std::size_t max_size;
	std::size_t position = 0;

	RubberSinkHandler &handler;

public:
	template<typename I>
	RubberSink(struct pool &_pool, RubberAllocation &&_a, std::size_t _max_size,
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
	std::size_t OnData(std::span<const std::byte> src) noexcept override;
	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset, std::size_t max_length,
				     bool then_eof) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;
};

static ssize_t
fd_read(FdType type, FileDescriptor fd, off_t offset,
	std::span<std::byte> dest) noexcept
{
	return IsAnySocket(type)
		? SocketDescriptor::FromFileDescriptor(fd).ReadNoWait(dest)
		: (IstreamHandler::HasOffset(offset)
		   ? fd.ReadAt(offset, dest)
		   : fd.Read(dest));
}

void
RubberSink::FailTooLarge() noexcept
{
	allocation = {};

	auto &_handler = handler;
	Destroy();
	_handler.RubberTooLarge();
}

void
RubberSink::DestroyEof() noexcept
{
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

std::size_t
RubberSink::OnData(std::span<const std::byte> src) noexcept
{
	assert(position <= max_size);

	if (position + src.size() > max_size) {
		/* too large, abort and invoke handler */

		FailTooLarge();
		return 0;
	}

	std::byte *p = (std::byte *)allocation.Write();
	std::copy(src.begin(), src.end(), p + position);
	position += src.size();

	return src.size();
}

IstreamDirectResult
RubberSink::OnDirect(FdType type, FileDescriptor fd, off_t offset,
		     std::size_t max_length,
		     [[maybe_unused]] bool then_eof) noexcept
{
	assert(position <= max_size);

	std::size_t length = max_size - position;
	if (length == 0) {
		/* already full, see what the file descriptor says */

		std::byte dummy[1];
		ssize_t nbytes = fd_read(type, fd, offset, dummy);
		if (nbytes > 0) {
			input.ConsumeDirect(nbytes);
			FailTooLarge();
			return IstreamDirectResult::CLOSED;
		}

		if (nbytes == 0) {
			DestroyEof();
			return IstreamDirectResult::CLOSED;
		}

		return IstreamDirectResult::ERRNO;
	}

	if (length > max_length)
		length = max_length;

	std::byte *p = (std::byte *)allocation.Write();
	p += position;

	ssize_t nbytes = fd_read(type, fd, offset, {p, length});
	if (nbytes <= 0)
		return nbytes < 0
			? IstreamDirectResult::ERRNO
			: IstreamDirectResult::END;

	input.ConsumeDirect(nbytes);
	position += (std::size_t)nbytes;

	return IstreamDirectResult::OK;
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
	Destroy();
}

/*
 * constructor
 *
 */

RubberSink *
sink_rubber_new(struct pool &pool, UnusedIstreamPtr input,
		Rubber &rubber, std::size_t max_size,
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

	const std::size_t allocate = size == -1
		? max_size
		: (std::size_t)size;

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
