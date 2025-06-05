// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "IstreamFilterTest.hxx"
#include "istream/SubstIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"
#include "istream/UnusedPtr.hxx"

class IstreamSubstTestTraits {
public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "xyz bar fo fo bar bla! fo",
		.enable_buckets = false, // TODO enable this once SubsIstream::_ConsumeBucketList() and _GetAvailable() is implemented properly
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "xyz foo fo fo bar blablablablubb fo");
	}

	UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		SubstTree tree;
		tree.Add(pool, "foo", "bar");
		tree.Add(pool, "blablablubb", "!");

		return UnusedIstreamPtr(istream_subst_new(&pool, std::move(input), std::move(tree)));
	}
};

INSTANTIATE_TYPED_TEST_SUITE_P(Subst, IstreamFilterTest,
			       IstreamSubstTestTraits);
