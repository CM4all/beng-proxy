// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "FailIstream.hxx"
#include "istream.hxx"
#include "New.hxx"

class FailIstream final : public Istream {
	std::exception_ptr error;

public:
	FailIstream(struct pool &p, std::exception_ptr &&_error)
		:Istream(p), error(std::move(_error)) {}

	/* virtual methods from class Istream */

	void _Read() noexcept override {
		assert(error);
		DestroyError(std::move(error));
	}

	void _FillBucketList(IstreamBucketList &) override {
		auto copy = std::move(error);
		Destroy();
		std::rethrow_exception(copy);
	}
};

UnusedIstreamPtr
istream_fail_new(struct pool &pool, std::exception_ptr &&ep) noexcept
{
	assert(ep);

	return NewIstreamPtr<FailIstream>(pool, std::move(ep));
}
