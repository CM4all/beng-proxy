// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "FailIstream.hxx"
#include "istream.hxx"
#include "New.hxx"

class FailIstream final : public Istream {
	const std::exception_ptr error;

public:
	FailIstream(struct pool &p, std::exception_ptr _error)
		:Istream(p), error(_error) {}

	/* virtual methods from class Istream */

	void _Read() noexcept override {
		assert(error);
		DestroyError(error);
	}

	void _FillBucketList(IstreamBucketList &) override {
		auto copy = error;
		Destroy();
		std::rethrow_exception(copy);
	}
};

UnusedIstreamPtr
istream_fail_new(struct pool &pool, std::exception_ptr ep) noexcept
{
	assert(ep);

	return NewIstreamPtr<FailIstream>(pool, ep);
}
