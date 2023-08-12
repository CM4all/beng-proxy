// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "InjectIstream.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "Bucket.hxx"
#include "New.hxx"

class InjectIstream final
	: public ForwardIstream, public InjectIstreamControl {

public:
	InjectIstream(struct pool &p, UnusedIstreamPtr &&_input) noexcept
		:ForwardIstream(p, std::move(_input)) {}

	void InjectFault(std::exception_ptr ep) noexcept {
		DestroyError(ep);
	}

	/* virtual methods from class Istream */

	off_t _GetAvailable(bool partial) noexcept override {
		/* never return the total length, because the caller may then
		   make assumptions on when this stream ends */
		return partial && HasInput()
			? ForwardIstream::_GetAvailable(partial)
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
		return -1;
	}

	/* virtual methods from class IstreamHandler */

	void OnEof() noexcept override {
		ClearInput();
	}

	void OnError(std::exception_ptr) noexcept override {
		ClearInput();
	}
};

void
InjectIstreamControl::InjectFault(std::exception_ptr e) noexcept
{
	auto &d = *(InjectIstream *)this;
	d.InjectFault(std::move(e));
}

/*
 * constructor
 *
 */

std::pair<UnusedIstreamPtr, InjectIstreamControl &>
istream_inject_new(struct pool &pool, UnusedIstreamPtr input) noexcept
{
	auto *i = NewIstream<InjectIstream>(pool, std::move(input));
	InjectIstreamControl &control = *i;
	return {UnusedIstreamPtr(i), control};
}
