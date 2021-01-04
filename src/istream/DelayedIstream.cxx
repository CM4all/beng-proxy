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

#include "DelayedIstream.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "Bucket.hxx"
#include "New.hxx"
#include "event/DeferEvent.hxx"

#include <cassert>

class DelayedIstream final
	: public ForwardIstream, public DelayedIstreamControl {

	DeferEvent defer_read;

	FdTypeMask direct_mask = 0;

public:
	explicit DelayedIstream(struct pool &p, EventLoop &event_loop) noexcept
		:ForwardIstream(p),
		 defer_read(event_loop, BIND_THIS_METHOD(DeferredRead)) {
	}

	void Set(UnusedIstreamPtr _input) noexcept {
		assert(!HasInput());

		SetInput(std::move(_input));
		input.SetDirect(direct_mask);

		if (HasHandler())
			defer_read.Schedule();
	}

	void SetEof() noexcept {
		assert(!HasInput());

		DestroyEof();
	}

	void SetError(std::exception_ptr ep) noexcept {
		assert(!HasInput());

		DestroyError(ep);
	}

private:
	void DeferredRead() noexcept {
		input.Read();
	}

public:
	/* virtual methods from class Istream */

	void _SetDirect(FdTypeMask mask) noexcept override {
		direct_mask = mask;

		if (HasInput())
			input.SetDirect(mask);
	}

	off_t _GetAvailable(bool partial) noexcept override {
		return HasInput()
			? ForwardIstream::_GetAvailable(partial)
			: -1;
	}

	off_t _Skip(off_t length) noexcept override {
		return HasInput()
			? ForwardIstream::_Skip(length)
			: -1;
	}

	void _Read() noexcept override {
		if (HasInput())
			ForwardIstream::_Read();
	}

	void _FillBucketList(IstreamBucketList &list) override {
		if (HasInput()) {
			try {
				input.FillBucketList(list);
			} catch (...) {
				Destroy();
				throw;
			}
		} else
			list.SetMore();
	}

	int _AsFd() noexcept override {
		return HasInput()
			? ForwardIstream::_AsFd()
			: -1;
	}

	void _Close() noexcept override {
		if (HasInput())
			ForwardIstream::_Close();
		else {
			if (cancel_ptr)
				cancel_ptr.Cancel();

			Destroy();
		}
	}
};

void
DelayedIstreamControl::Set(UnusedIstreamPtr _input) noexcept
{
	auto &d = *(DelayedIstream *)this;
	d.Set(std::move(_input));
}

void
DelayedIstreamControl::SetEof() noexcept
{
	auto &d = *(DelayedIstream *)this;
	d.SetEof();
}

void
DelayedIstreamControl::SetError(std::exception_ptr e) noexcept
{
	auto &d = *(DelayedIstream *)this;
	d.SetError(std::move(e));
}

std::pair<UnusedIstreamPtr, DelayedIstreamControl &>
istream_delayed_new(struct pool &pool, EventLoop &event_loop) noexcept
{
	auto *i = NewIstream<DelayedIstream>(pool, event_loop);
	DelayedIstreamControl &control = *i;
	return {UnusedIstreamPtr(i), control};
}
