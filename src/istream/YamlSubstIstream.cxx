// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "YamlSubstIstream.hxx"
#include "SubstIstream.hxx"
#include "UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "util/IterableSplitString.hxx"

#include <yaml-cpp/node/parse.h>
#include <yaml-cpp/node/node.h>
#include <yaml-cpp/node/iterator.h>
#include <yaml-cpp/node/impl.h>
#include <yaml-cpp/node/convert.h>
#include <yaml-cpp/node/detail/impl.h>

#include <assert.h>

static YAML::Node
ResolveYamlPathSegment(const YAML::Node &parent, std::string_view segment)
{
	if (parent.IsMap()) {
		auto result = parent[std::string{segment}.c_str()];
		if (!result)
			throw FmtRuntimeError("YAML path segment '{}' does not exist", segment);

		return result;
	} else
		throw FmtRuntimeError("Failed to resolve YAML path segment '{}'", segment);
}

static YAML::Node
ResolveYamlPath(YAML::Node node, std::string_view path)
{
	for (const auto s : IterableSplitString(path, '.')) {
		if (s.empty())
			continue;

		node = ResolveYamlPathSegment(node, s);
	}

	return node;
}

static YAML::Node
ResolveYamlMap(YAML::Node node, std::string_view path)
{
	node = ResolveYamlPath(node, path);
	if (!node.IsMap())
		throw path.empty()
			? std::runtime_error("Not a YAML map")
			: FmtRuntimeError("Path '{}' is not a YAML map", path);

	return node;
}

static auto
MakePrefix(bool alt_syntax, const char *_prefix)
{
	std::string prefix = alt_syntax ? "{[" : "{%";
	if (_prefix != nullptr)
		prefix += _prefix;
	return prefix;
}

static void
LoadYamlMap(struct pool &pool, SubstTree &tree,
	    const std::string &prefix,
	    const std::string &suffix,
	    const YAML::Node &node) noexcept
{
	assert(node.IsMap());

	for (const auto &i : node) {
		if (!i.first.IsScalar())
			continue;

		if (i.second.IsScalar()) {
			const auto name = prefix + i.first.as<std::string>() + suffix;
			const auto value = i.second.as<std::string>();
			tree.Add(pool, p_strndup(&pool, name.data(), name.length()),
				 {p_strndup(&pool, value.data(), value.length()), value.length()});
		} else if (i.second.IsMap()) {
			LoadYamlMap(pool, tree, prefix + i.first.as<std::string>() + ".",
				    suffix,
				    i.second);
		}
	}
}

static SubstTree
LoadYamlMap(struct pool &pool, bool alt_syntax, const char *_prefix,
	    const YAML::Node &node) noexcept
{
	assert(node.IsMap());

	const auto prefix = MakePrefix(alt_syntax, _prefix);
	const std::string suffix(alt_syntax ? "]}" : "%}");

	SubstTree tree;
	LoadYamlMap(pool, tree, prefix, suffix, node);
	return tree;
}

UnusedIstreamPtr
NewYamlSubstIstream(struct pool &pool, UnusedIstreamPtr input,
		    bool alt_syntax,
		    const char *prefix,
		    const YAML::Node &yaml_node, const char *yaml_map_path)
{
	return istream_subst_new(&pool, std::move(input),
				 LoadYamlMap(pool, alt_syntax, prefix,
					     ResolveYamlMap(yaml_node,
							    yaml_map_path)));
}

static SubstTree
LoadYamlFile(struct pool &pool, bool alt_syntax,
	     const char *prefix,
	     const char *file_path, const char *map_path)
	try {
		return LoadYamlMap(pool, alt_syntax, prefix,
				   ResolveYamlMap(YAML::LoadFile(file_path), map_path));
	} catch (...) {
		std::throw_with_nested(FmtRuntimeError("Failed to load YAML file '{}'",
						       file_path));
	}

UnusedIstreamPtr
NewYamlSubstIstream(struct pool &pool, UnusedIstreamPtr input,
		    bool alt_syntax,
		    const char *prefix,
		    const char *yaml_file, const char *yaml_map_path)
{
	return istream_subst_new(&pool, std::move(input),
				 LoadYamlFile(pool, alt_syntax, prefix,
					      yaml_file, yaml_map_path));
}
