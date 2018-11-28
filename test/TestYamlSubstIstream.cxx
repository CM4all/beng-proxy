/*
 * Copyright 2007-2018 Content Management AG
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

#include "istream/YamlSubstIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream.hxx"
#include "istream/UnusedPtr.hxx"
#include "util/StringView.hxx"

#include <yaml-cpp/node/parse.h>
#include <yaml-cpp/node/node.h>
#include <yaml-cpp/node/impl.h>

#define EXPECTED_RESULT "Good morning, everybody! bar"

static constexpr char yaml[] =
    "top: level\n"
    "child:\n"
    "  grandchild:\n"
    "    greeting: Good morning\n"
    "    object: everybody\n"
    "    nested:\n"
    "      foo: bar\n";

class EventLoop;

static UnusedIstreamPtr
create_input(struct pool &pool) noexcept
{
    return istream_string_new(pool, "{[foo:greeting]}, {[foo:object]}! {[foo:nested.foo]}");
}

static UnusedIstreamPtr
create_test(EventLoop &, struct pool &pool, UnusedIstreamPtr input) noexcept
{
    return NewYamlSubstIstream(pool, std::move(input), true,
                               "foo:",
                               YAML::Load(yaml),
                               "child.grandchild");
}

#include "t_istream_filter.hxx"
