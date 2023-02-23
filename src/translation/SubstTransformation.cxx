// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "SubstTransformation.hxx"
#include "AllocatorPtr.hxx"

SubstTransformation::SubstTransformation(AllocatorPtr alloc,
					 const SubstTransformation &src) noexcept
	:prefix(alloc.CheckDup(src.prefix)),
	 yaml_file(alloc.CheckDup(src.yaml_file)),
	 yaml_map_path(alloc.CheckDup(src.yaml_map_path)) {}
