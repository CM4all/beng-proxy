// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Dissect.hxx"
#include "uri/Verify.hxx"
#include "util/StringSplit.hxx"

#include <string.h>

bool
DissectedUri::Parse(std::string_view src) noexcept
{
	const auto [before_query, _query] = Split(src, '?');
	query = _query;

	const auto [before_args, args_and_path_info] = Split(before_query, ';');
	base = before_args;

	if (!uri_path_verify(base))
		return false;

	/* TODO second semicolon for stuff being forwared? */
	const auto slash = args_and_path_info.find('/');
	if (slash != args_and_path_info.npos) {
		const auto [_args, _path_info] =
			Partition(args_and_path_info, slash);
		args = _args;
		path_info = _path_info;
	} else {
		args = args_and_path_info;
		path_info = {};
	}

	return true;
}
