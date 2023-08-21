// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

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
		switch (InvokeReady()) {
		case IstreamReadyResult::OK:
			break;

		case IstreamReadyResult::FALLBACK:
			input.Read();
			break;

		case IstreamReadyResult::CLOSED:
			break;
		}
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
		if (HasInput())
			ForwardIstream::_FillBucketList(list);
		else
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
