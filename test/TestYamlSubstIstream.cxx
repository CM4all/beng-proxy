// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "IstreamFilterTest.hxx"
#include "istream/YamlSubstIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"
#include "istream/UnusedPtr.hxx"

#include <yaml-cpp/node/parse.h>
#include <yaml-cpp/node/node.h>
#include <yaml-cpp/node/impl.h>

static constexpr char yaml[] =
	"top: level\n"
	"child:\n"
	"  grandchild:\n"
	"    greeting: Good morning\n"
	"    object: everybody\n"
	"    nested:\n"
	"      foo: bar\n";

class IstreamYamlSubstTestTraits {
public:
	static constexpr IstreamFilterTestOptions options{
		.expected_result = "Good morning, everybody! bar",
		.enable_buckets = false, // TODO enable this once SubsIstream::_ConsumeBucketList() and _GetAvailable() is implemented properly
	};

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_string_new(pool, "{[foo:greeting]}, {[foo:object]}! {[foo:nested.foo]}");
	}

	UnusedIstreamPtr CreateTest(EventLoop &, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		return NewYamlSubstIstream(pool, std::move(input), true,
					   "foo:",
					   YAML::Load(yaml),
					   "child.grandchild");
	}
};

INSTANTIATE_TYPED_TEST_CASE_P(YamlSubst, IstreamFilterTest,
			      IstreamYamlSubstTestTraits);
