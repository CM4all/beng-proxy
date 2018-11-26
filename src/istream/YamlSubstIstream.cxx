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
#include "util/IterableSplitString.hxx"
#include "util/RuntimeError.hxx"
#include "util/StringView.hxx"

#include <yaml-cpp/node/parse.h>
#include <yaml-cpp/node/node.h>
#include <yaml-cpp/node/iterator.h>
#include <yaml-cpp/node/impl.h>
#include <yaml-cpp/node/convert.h>
#include <yaml-cpp/node/detail/impl.h>

static YAML::Node
ResolveYamlPathSegment(const YAML::Node &parent, StringView segment)
{
    if (parent.IsMap()) {
        auto result = parent[std::string(segment.data, segment.size).c_str()];
        if (!result)
            throw FormatRuntimeError("YAML path segment '%.*s' does not exist",
                                     int(segment.size), segment.data);

        return result;
    } else
        throw FormatRuntimeError("Failed to resolve YAML path segment '%.*s'",
                                 int(segment.size), segment.data);
}

static YAML::Node
ResolveYamlPath(YAML::Node node, StringView path)
{
    for (StringView s : IterableSplitString(path, '/')) {
        if (s.empty())
            continue;

        node = ResolveYamlPathSegment(node, s);
    }

    return node;
}

static YAML::Node
ResolveYamlMap(YAML::Node node, StringView path)
{
    node = ResolveYamlPath(node, path);
    if (!node.IsMap())
        throw path.empty()
            ? std::runtime_error("Not a YAML map")
            : FormatRuntimeError("Path '%.*s' is not a YAML map",
                                 int(path.size), path.data);

    return node;
}

static auto
MakePrefix(const char *_prefix)
{
    std::string prefix = "{{";
    if (_prefix != nullptr)
        prefix += _prefix;
    return prefix;
}

static SubstTree
LoadYamlMap(struct pool &pool, const char *_prefix,
            const YAML::Node &node) noexcept
{
    assert(node.IsMap());

    const auto prefix = MakePrefix(_prefix);

    SubstTree tree;

    for (const auto &i : node) {
        if (!i.first.IsScalar() || !i.second.IsScalar())
            continue;

        const auto name = prefix + i.first.as<std::string>() + "}}";
        const auto value = i.second.as<std::string>();
        tree.Add(pool, p_strndup(&pool, name.data(), name.length()),
                 {p_strndup(&pool, value.data(), value.length()), value.length()});
    }

    return tree;
}

UnusedIstreamPtr
NewYamlSubstIstream(struct pool &pool, UnusedIstreamPtr input,
                    const char *prefix,
                    const YAML::Node &yaml_node, const char *yaml_map_path)
{
    return istream_subst_new(&pool, std::move(input),
                             LoadYamlMap(pool, prefix,
                                         ResolveYamlMap(yaml_node,
                                                        yaml_map_path)));
}

static SubstTree
LoadYamlFile(struct pool &pool, const char *prefix,
             const char *file_path, const char *map_path)
try {
    return LoadYamlMap(pool, prefix,
                       ResolveYamlMap(YAML::LoadFile(file_path), map_path));
} catch (...) {
    std::throw_with_nested(FormatRuntimeError("Failed to load YAML file '%s'",
                                              file_path));
}

UnusedIstreamPtr
NewYamlSubstIstream(struct pool &pool, UnusedIstreamPtr input,
                    const char *prefix,
                    const char *yaml_file, const char *yaml_map_path)
{
    return istream_subst_new(&pool, std::move(input),
                             LoadYamlFile(pool, prefix,
                                          yaml_file, yaml_map_path));
}
