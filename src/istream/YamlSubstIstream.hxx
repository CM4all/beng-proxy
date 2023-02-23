// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

struct pool;
class UnusedIstreamPtr;
namespace YAML { class Node; }

UnusedIstreamPtr
NewYamlSubstIstream(struct pool &pool, UnusedIstreamPtr input, bool alt_syntax,
		    const char *prefix,
		    const YAML::Node &yaml_node, const char *yaml_map_path);

/**
 * Substitute variables in the form "{[NAME]}" with values from the
 * given YAML file.
 *
 * Throws on error (if the YAML file could not be loaded).
 */
UnusedIstreamPtr
NewYamlSubstIstream(struct pool &pool, UnusedIstreamPtr input, bool alt_syntax,
		    const char *prefix,
		    const char *yaml_file, const char *yaml_map_path);
