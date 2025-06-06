// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "ToBucketIstream.hxx"
#include "Bucket.hxx"
#include "UnusedPtr.hxx"
#include "memory/fb_pool.hxx"

#include <utility> // for std::unreachable()

#include <string.h>

ToBucketIstream::ToBucketIstream(struct pool &_pool,
				 EventLoop &_event_loop,
				 UnusedIstreamPtr &&_input) noexcept
	:FacadeIstream(_pool, std::move(_input)),
	 defer_read(_event_loop, BIND_THIS_METHOD(DeferredRead)) {}

[[gnu::noreturn]]
void
ToBucketIstream::_Read() noexcept
{
	std::unreachable();
}

void
ToBucketIstream::_FillBucketList(IstreamBucketList &list)
{
	auto r = buffer.Read();
	if (!r.empty()) {
		list.Push(r);
		list.SetMore();
		return;
	}

	if (!HasInput())
		return;

	IstreamBucketList tmp;
	FillBucketListFromInput(tmp);
	if (tmp.IsEmpty()) {
		if (tmp.HasMore()) {
			/* no data yet or FillBucketList() not implemented: invoke
			   its old-style Read() method */
			defer_read.Schedule();
			list.SetMore();
		} else {
			/* end of file */
			CloseInput();
		}

		return;
	}

	list.SpliceFrom(std::move(tmp));
}

Istream::ConsumeBucketResult
ToBucketIstream::_ConsumeBucketList(size_t nbytes) noexcept
{
	if (!buffer.empty()) {
		size_t available = buffer.GetAvailable();
		size_t consumed = std::min(nbytes, available);
		buffer.Consume(consumed);
		buffer.FreeIfEmpty();
		return {consumed, buffer.empty()};
	}

	if (HasInput())
		return input.ConsumeBucketList(nbytes);

	return {0, true};
}

IstreamReadyResult
ToBucketIstream::OnIstreamReady() noexcept
{
	defer_read.Cancel();
	return InvokeReady();
}

size_t
ToBucketIstream::OnData(std::span<const std::byte> src) noexcept
{
	defer_read.Cancel();

	buffer.AllocateIfNull(fb_pool_get());
	return buffer.MoveFrom(src);
}

void
ToBucketIstream::OnEof() noexcept
{
	ClearInput();
	DestroyEof();
}

void
ToBucketIstream::OnError(std::exception_ptr ep) noexcept
{
	ClearInput();
	DestroyError(std::move(ep));
}
