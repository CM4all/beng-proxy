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

#include "ToBucketIstream.hxx"
#include "Bucket.hxx"
#include "UnusedPtr.hxx"
#include "fb_pool.hxx"

#include <string.h>

ToBucketIstream::ToBucketIstream(struct pool &_pool,
				 EventLoop &_event_loop,
				 UnusedIstreamPtr &&_input) noexcept
	:FacadeIstream(_pool, std::move(_input)),
	 defer_read(_event_loop, BIND_THIS_METHOD(DeferredRead)) {}

gcc_noreturn
void
ToBucketIstream::_Read() noexcept
{
	gcc_unreachable();
}

void
ToBucketIstream::_FillBucketList(IstreamBucketList &list)
{
	auto r = buffer.Read();
	if (!r.empty()) {
		list.Push(r.ToVoid());
		list.SetMore();
		return;
	}

	if (!HasInput())
		return;

	IstreamBucketList tmp;
	input.FillBucketList(tmp);
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

size_t
ToBucketIstream::_ConsumeBucketList(size_t nbytes) noexcept
{
	if (!buffer.empty()) {
		size_t available = buffer.GetAvailable();
		size_t consumed = std::min(nbytes, available);
		buffer.Consume(consumed);
		buffer.FreeIfEmpty();
		return consumed;
	}

	if (HasInput())
		return input.ConsumeBucketList(nbytes);

	return 0;
}

bool
ToBucketIstream::OnIstreamReady() noexcept
{
	defer_read.Cancel();
	return InvokeReady();
}

size_t
ToBucketIstream::OnData(const void *data, size_t length) noexcept
{
	defer_read.Cancel();

	buffer.AllocateIfNull(fb_pool_get());
	auto w = buffer.Write();
	size_t nbytes = std::min(length, w.size);
	memcpy(w.data, data, nbytes);
	return nbytes;
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
