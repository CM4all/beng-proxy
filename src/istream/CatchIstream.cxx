// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "CatchIstream.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "Bucket.hxx"
#include "New.hxx"

#include <memory>

#include <assert.h>

class CatchIstream final : public ForwardIstream {
	/**
	 * This much data was announced by our input, either by
	 * GetAvailable(), OnData() or OnDirect().
	 */
	off_t available = 0;

	/**
	 * The amount of data passed to OnData(), minus the number of
	 * bytes consumed by it.  The next call must be at least this big.
	 */
	std::size_t chunk = 0;

	const BoundMethod<std::exception_ptr(std::exception_ptr ep) noexcept> callback;

public:
	CatchIstream(struct pool &_pool, UnusedIstreamPtr _input,
		     BoundMethod<std::exception_ptr(std::exception_ptr ep) noexcept> _callback) noexcept
		:ForwardIstream(_pool, std::move(_input)),
		 callback(_callback) {}

	void SendSpace() noexcept;

	/* virtual methods from class Istream */

	off_t _GetAvailable(bool partial) noexcept override;

	off_t _Skip(off_t length) noexcept override {
		off_t nbytes = ForwardIstream::_Skip(length);
		if (nbytes > 0) {
			if (nbytes < available)
				available -= nbytes;
			else
				available = 0;

			if ((std::size_t)nbytes < chunk)
				chunk -= nbytes;
			else
				chunk = 0;
		}

		return nbytes;
	}

	void _Read() noexcept override;
	void _FillBucketList(IstreamBucketList &list) override;
	void _ConsumeDirect(std::size_t nbytes) noexcept override;

	/* virtual methods from class IstreamHandler */

	std::size_t OnData(std::span<const std::byte> src) noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;
};

static constexpr char space[] =
	"                                "
	"                                "
	"                                "
	"                                ";

void
CatchIstream::SendSpace() noexcept
{
	assert(!HasInput());
	assert(available > 0);
	assert((off_t)chunk <= available);

	if (chunk > sizeof(space) - 1) {
		std::unique_ptr<char[]> buffer(new char[chunk]);
		std::fill_n(buffer.get(), ' ', chunk);
		std::size_t nbytes = ForwardIstream::OnData(std::as_bytes(std::span{buffer.get(), chunk}));
		if (nbytes == 0)
			return;

		chunk -= nbytes;
		available -= nbytes;

		if (chunk > 0)
			return;

		if (available == 0) {
			DestroyEof();
			return;
		}
	}

	do {
		std::size_t length;
		if (available >= (off_t)sizeof(space) - 1)
			length = sizeof(space) - 1;
		else
			length = (std::size_t)available;

		std::size_t nbytes = ForwardIstream::OnData(std::as_bytes(std::span{space, length}));
		if (nbytes == 0)
			return;

		available -= nbytes;
		if (nbytes < length)
			return;
	} while (available > 0);

	DestroyEof();
}

void
CatchIstream::_ConsumeDirect(std::size_t nbytes) noexcept
{
	ForwardIstream::_ConsumeDirect(nbytes);

	if ((off_t)nbytes < available)
		available -= (off_t)nbytes;
	else
		available = 0;

	if (nbytes < chunk)
		chunk -= nbytes;
	else
		chunk = 0;
}

/*
 * istream handler
 *
 */

std::size_t
CatchIstream::OnData(std::span<const std::byte> src) noexcept
{
	if ((off_t)src.size() > available)
		available = src.size();

	if (src.size() > chunk)
		chunk = src.size();

	std::size_t nbytes = ForwardIstream::OnData(src);
	if (nbytes > 0) {
		if ((off_t)nbytes < available)
			available -= (off_t)nbytes;
		else
			available = 0;

		chunk -= nbytes;
	}

	return nbytes;
}

void
CatchIstream::OnError(std::exception_ptr ep) noexcept
{
	ep = callback(ep);
	if (ep) {
		/* forward error to our handler */
		ForwardIstream::OnError(ep);
		return;
	}

	/* the error has been handled by the callback, and he has disposed
	   it */

	ClearInput();

	if (available > 0)
		/* according to a previous call to method "available", there
		   is more data which we must provide - fill that with space
		   characters */
		SendSpace();
	else
		DestroyEof();
}

/*
 * istream implementation
 *
 */

off_t
CatchIstream::_GetAvailable(bool partial) noexcept
{
	if (HasInput()) {
		off_t result = ForwardIstream::_GetAvailable(partial);
		if (result > available)
			available = result;

		return result;
	} else
		return available;
}

void
CatchIstream::_Read() noexcept
{
	if (HasInput())
		ForwardIstream::_Read();
	else if (available == 0)
		DestroyEof();
	else
		SendSpace();
}

void
CatchIstream::_FillBucketList(IstreamBucketList &list)
{
	if (!HasInput()) {
		// TODO: generate space bucket?
		list.EnableFallback();
		return;
	}

	try {
		input.FillBucketList(list);
	} catch (...) {
		if (auto error = callback(std::current_exception())) {
			Destroy();
			std::rethrow_exception(std::move(error));
		}

		/* the error has been handled by the callback, and he has
		   disposed it */
		list.EnableFallback();

		// TODO: return space bucket here
	}
}

/*
 * constructor
 *
 */

UnusedIstreamPtr
NewCatchIstream(struct pool *pool, UnusedIstreamPtr input,
		BoundMethod<std::exception_ptr(std::exception_ptr ep) noexcept> callback) noexcept
{
	assert(callback);

	return NewIstreamPtr<CatchIstream>(*pool, std::move(input), callback);
}
