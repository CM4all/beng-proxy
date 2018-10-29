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

#include "YamlSubstIstream.hxx"
#include "SubstIstream.hxx"
#include "FailIstream.hxx"
#include "UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "util/RuntimeError.hxx"
#include "util/StringView.hxx"

#include <yaml-cpp/node/parse.h>
#include <yaml-cpp/node/node.h>
#include <yaml-cpp/node/iterator.h>
#include <yaml-cpp/node/impl.h>

static SubstTree
LoadYamlMap(struct pool &pool, const YAML::Node &node) noexcept
{
    assert(node.IsMap());

    SubstTree tree;

    for (const auto &i : node) {
        if (!i.first.IsScalar() || !i.second.IsScalar())
            continue;

        const auto name = "{{" + i.first.as<std::string>() + "}}";
        const auto value = i.second.as<std::string>();
        tree.Add(pool, p_strndup(&pool, name.data(), name.length()),
                 {p_strndup(&pool, value.data(), value.length()), value.length()});
    }

    return tree;
}

static SubstTree
LoadYamlFile(struct pool &pool, const char *path)
{

    const auto root = YAML::LoadFile(path);
    if (!root.IsMap())
        throw std::runtime_error("File '%s' is not a YAML map");

    return LoadYamlMap(pool, root);
}

UnusedIstreamPtr
NewYamlSubstIstream(struct pool &pool, UnusedIstreamPtr input,
                    const char *yaml_file)
{
    return istream_subst_new(&pool, std::move(input),
                             LoadYamlFile(pool, yaml_file));
}
