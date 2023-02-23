// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

class AllocatorPtr;

struct SubstTransformation {
	const char *prefix;

	const char *yaml_file;

	const char *yaml_map_path;

	SubstTransformation(const char *_prefix,
			    const char *_yaml_file,
			    const char *_yaml_map_path) noexcept
		:prefix(_prefix),
		 yaml_file(_yaml_file), yaml_map_path(_yaml_map_path) {}

	SubstTransformation(AllocatorPtr alloc,
			    const SubstTransformation &src) noexcept;
};
