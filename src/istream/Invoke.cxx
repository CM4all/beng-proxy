// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "istream.hxx"
#include "Handler.hxx"
#include "io/FileDescriptor.hxx"

#include <cassert>

IstreamReadyResult
Istream::InvokeReady() noexcept
{
	assert(!destroyed);
	assert(handler != nullptr);
	assert(!in_data);
	assert(!eof);
	assert(!closing);

#ifndef NDEBUG
	const DestructObserver destructed(*this);
#endif

	const auto result = handler->OnIstreamReady();

#ifndef NDEBUG
	if (destructed || destroyed) {
		assert(result == IstreamReadyResult::CLOSED);
	} else {
		assert(result != IstreamReadyResult::CLOSED);
		assert(!closing);
		assert(!eof);
	}
#endif

	return result;
}

std::size_t
Istream::InvokeData(std::span<const std::byte> src) noexcept
{
	assert(!destroyed);
	assert(handler != nullptr);
	assert(src.data() != nullptr);
	assert(!src.empty());
	assert(!in_data);
	assert(!eof);
	assert(!closing);
	assert(src.size() >= data_available);
	assert(!available_full_set ||
	       std::cmp_less_equal(src.size(), available_full));

#ifndef NDEBUG
	const DestructObserver destructed(*this);
	in_data = true;
	in_direct = false;
#endif

	std::size_t nbytes = handler->OnData(src);
	assert(nbytes <= src.size());
	assert(nbytes == 0 || !eof);

#ifndef NDEBUG
	if (destructed || destroyed) {
		assert(nbytes == 0);
		return nbytes;
	}

	in_data = false;

	if (nbytes > 0)
		Consumed(nbytes);

	data_available = src.size() - nbytes;
#endif

	return nbytes;
}

IstreamDirectResult
Istream::InvokeDirect(FdType type, FileDescriptor fd, off_t offset,
		      std::size_t max_length, bool then_eof) noexcept
{
	assert(!destroyed);
	assert(handler != nullptr);
	assert(fd.IsDefined());
	assert(max_length > 0);
	assert(!in_data);
	assert(!eof);
	assert(!closing);
	assert(!available_full_set || !then_eof ||
	       std::cmp_equal(max_length, available_full));
	assert(!then_eof ||
	       std::cmp_greater_equal(max_length, available_partial));

#ifndef NDEBUG
	const DestructObserver destructed(*this);
	in_data = true;
	in_direct = true;
#endif

	const auto result = handler->OnDirect(type, fd, offset, max_length, then_eof);
	assert(result == IstreamDirectResult::CLOSED || !eof);

#ifndef NDEBUG
	if (destructed || destroyed) {
		assert(result == IstreamDirectResult::CLOSED);
		return result;
	}

	assert(result != IstreamDirectResult::CLOSED);

	in_data = false;
#endif

	return result;
}

IstreamHandler &
Istream::PrepareEof() noexcept
{
	assert(!destroyed);
	assert(!eof);
	assert(!closing);
	assert(data_available == 0);
	assert(available_partial == 0);
	assert(!available_full_set || available_full == 0);
	assert(handler != nullptr);

#ifndef NDEBUG
	eof = true;
	in_direct = false;
#endif

	return *handler;
}

void
Istream::InvokeEof() noexcept
{
	PrepareEof().OnEof();
}

void
Istream::DestroyEof() noexcept
{
	auto &_handler = PrepareEof();
	Destroy();
	_handler.OnEof();
}

IstreamHandler &
Istream::PrepareError() noexcept
{
	assert(!destroyed);
	assert(!eof);
	assert(!closing);
	assert(handler != nullptr);

#ifndef NDEBUG
	eof = true;
	in_direct = false;
#endif

	return *handler;
}

void
Istream::InvokeError(std::exception_ptr ep) noexcept
{
	assert(ep);

	PrepareError().OnError(std::move(ep));
}

void
Istream::DestroyError(std::exception_ptr ep) noexcept
{
	auto &_handler = PrepareError();
	Destroy();
	_handler.OnError(std::move(ep));
}
