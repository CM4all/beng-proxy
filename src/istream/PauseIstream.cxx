// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "PauseIstream.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"
#include "Bucket.hxx"
#include "event/DeferEvent.hxx"

class PauseIstream final : public ForwardIstream {
	const SharedPoolPtr<PauseIstreamControl> control;

	DeferEvent defer_read;

	bool want_read = false;

	bool resumed = false;

public:
	PauseIstream(struct pool &p, EventLoop &event_loop,
		     UnusedIstreamPtr _input) noexcept
		:ForwardIstream(p, std::move(_input)),
		 control(SharedPoolPtr<PauseIstreamControl>::Make(p, *this)),
		 defer_read(event_loop, BIND_THIS_METHOD(DeferredRead)) {}

	~PauseIstream() noexcept override {
		control->pause = nullptr;
	}

	auto GetControl() noexcept {
		return control;
	}

	void Resume() noexcept {
		resumed = true;

		if (want_read)
			/* there is a pending read request; schedule it to be
			   executed (but outside of this stack frame) */
			defer_read.Schedule();
	}

private:
	void DeferredRead() noexcept {
		ForwardIstream::_Read();
	}

protected:
	/* virtual methods from class Istream */

	void _Read() noexcept override {
		if (resumed) {
			defer_read.Cancel();
			ForwardIstream::_Read();
		} else {
			/* we'll try again after Resume() gets called */
			want_read = true;
		}
	}

	void _FillBucketList(IstreamBucketList &list) override {
		if (resumed) {
			defer_read.Cancel();
			ForwardIstream::_FillBucketList(list);
		} else {
			list.SetPushMore();

			/* we'll try again after Resume() gets called */
			want_read = true;
		}
	}
};

void
PauseIstreamControl::Resume() noexcept
{
	if (pause != nullptr)
		pause->Resume();
}

std::pair<UnusedIstreamPtr, SharedPoolPtr<PauseIstreamControl>>
NewPauseIstream(struct pool &pool, EventLoop &event_loop,
		UnusedIstreamPtr input) noexcept
{
	auto *i = NewIstream<PauseIstream>(pool, event_loop, std::move(input));
	return std::make_pair(UnusedIstreamPtr(i), i->GetControl());
}
