// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "InjectIstream.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "Bucket.hxx"
#include "New.hxx"

#include <cassert>

class InjectIstream final : public ForwardIstream {
	const std::shared_ptr<InjectIstreamControl> control;

public:
	InjectIstream(struct pool &p, UnusedIstreamPtr &&_input) noexcept
		:ForwardIstream(p, std::move(_input)),
		 control(std::make_shared<InjectIstreamControl>(*this)) {}

	~InjectIstream() noexcept override {
		assert(control);
		assert(control->inject == this);

		control->inject = nullptr;
	}

	auto GetControl() noexcept {
		return control;
	}

	void InjectFault(std::exception_ptr &&ep) noexcept {
		DestroyError(std::move(ep));
	}

	/* virtual methods from class Istream */

	IstreamLength _GetLength() noexcept override {
		auto result = ForwardIstream::_GetLength();

		/* never return the total length, because the caller may then
		   make assumptions on when this stream ends */
		result.exhaustive = false;

		return result;
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

	/* virtual methods from class IstreamHandler */

	void OnEof() noexcept override {
		ClearInput();
	}

	void OnError(std::exception_ptr &&) noexcept override {
		ClearInput();
	}
};

std::pair<UnusedIstreamPtr, std::shared_ptr<InjectIstreamControl>>
istream_inject_new(struct pool &pool, UnusedIstreamPtr input) noexcept
{
	auto *i = NewIstream<InjectIstream>(pool, std::move(input));
	return std::make_pair(UnusedIstreamPtr{i}, i->GetControl());
}

bool
InjectFault(std::shared_ptr<InjectIstreamControl> &&control, std::exception_ptr &&e) noexcept
{
	if (!control)
		return false;

	InjectIstream *inject = control->inject;
	control.reset();
	if (!inject)
		return false;

	inject->InjectFault(std::move(e));
	return true;
}
