// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "FifoBufferSink.hxx"
#include "Bucket.hxx"
#include "memory/fb_pool.hxx"
#include "io/Buffered.hxx"
#include "io/FileDescriptor.hxx"

#include <algorithm>
#include <exception>

#include <string.h>
#include <unistd.h>

IstreamReadyResult
FifoBufferSink::OnIstreamReady() noexcept
{
	IstreamBucketList list;

	try {
		input.FillBucketList(list);
	} catch (...) {
		handler.OnFifoBufferSinkError(std::current_exception());
		return IstreamReadyResult::CLOSED;
	}

	std::size_t nbytes = 0;
	IstreamReadyResult result = IstreamReadyResult::OK;
	bool more = list.HasMore();

	for (const auto &bucket : list) {
		if (!bucket.IsBuffer()) {
			result = IstreamReadyResult::FALLBACK;
			more = true;
			break;
		}

		buffer.AllocateIfNull(fb_pool_get());
		auto r = bucket.GetBuffer();
		std::size_t n_copy = buffer.MoveFrom(r);
		nbytes += n_copy;

		if (n_copy < r.size()) {
			more = true;
			break;
		}
	}

	if (nbytes > 0 && input.ConsumeBucketList(nbytes).eof)
		more = false;

	if (!more) {
		CloseInput();
		handler.OnFifoBufferSinkEof();
		return IstreamReadyResult::CLOSED;
	}

	if (!handler.OnFifoBufferSinkData())
		return IstreamReadyResult::CLOSED;

	if (list.ShouldFallback())
		result = IstreamReadyResult::FALLBACK;

	return result;
}

std::size_t
FifoBufferSink::OnData(std::span<const std::byte> src) noexcept
{
	buffer.AllocateIfNull(fb_pool_get());
	const std::size_t nbytes = buffer.MoveFrom(src);

	if (!handler.OnFifoBufferSinkData())
		return 0;

	return nbytes;
}

IstreamDirectResult
FifoBufferSink::OnDirect(FdType, FileDescriptor fd, off_t offset,
			 std::size_t max_length,
			 [[maybe_unused]] bool then_eof) noexcept
{
	buffer.AllocateIfNull(fb_pool_get());

	const auto nbytes = HasOffset(offset)
		? ReadToBufferAt(fd, offset, buffer, max_length)
		: ReadToBuffer(fd, buffer, max_length);
	if (nbytes == -2)
		return IstreamDirectResult::BLOCKING;

	if (nbytes <= 0) {
		buffer.FreeIfEmpty();
		return nbytes < 0
			? IstreamDirectResult::ERRNO
			: IstreamDirectResult::END;
	}

	input.ConsumeDirect(nbytes);

	if (!handler.OnFifoBufferSinkData())
		return IstreamDirectResult::CLOSED;

	return IstreamDirectResult::OK;
}

void
FifoBufferSink::OnEof() noexcept
{
	input.Clear();
	handler.OnFifoBufferSinkEof();
}

void
FifoBufferSink::OnError(std::exception_ptr &&ep) noexcept
{
	input.Clear();
	handler.OnFifoBufferSinkError(std::move(ep));
}
