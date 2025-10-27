// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "SecondFailIstream.hxx"
#include "Bucket.hxx"
#include "ForwardIstream.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"

#include <cassert>

class SecondFailIstream final : public ForwardIstream {
	std::exception_ptr error;

	bool second = false;

public:
	SecondFailIstream(struct pool &p, UnusedIstreamPtr &&_input,
			  std::exception_ptr &&_error)
		:ForwardIstream(p, std::move(_input)),
		 error(std::move(_error))
	{
		assert(error);
	}

	/* virtual methods from class Istream */

	void _Read() noexcept override {
		assert(error);

		if (second) {
			DestroyError(std::move(error));
		} else {
			second = true;
			ForwardIstream::_Read();
		}
	}

	void _FillBucketList(IstreamBucketList &list) override {
		assert(error);

		if (second) {
			auto copy = std::move(error);
			Destroy();
			std::rethrow_exception(std::move(copy));
		} else {
			second = true;
			ForwardIstream::_FillBucketList(list);
			list.SetPushMore();
		}
	}
};

UnusedIstreamPtr
NewSecondFailIstream(struct pool &pool, UnusedIstreamPtr &&input,
		     std::exception_ptr &&error) noexcept
{
	return NewIstreamPtr<SecondFailIstream>(pool, std::move(input), std::move(error));
}
