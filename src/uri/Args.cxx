// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Args.hxx"
#include "PEscape.hxx"
#include "uri/Escape.hxx"
#include "strmap.hxx"
#include "AllocatorPtr.hxx"
#include "util/IterableSplitString.hxx"
#include "util/StringSplit.hxx"

#include <string.h>

static constexpr char ARGS_ESCAPE_CHAR = '$';

StringMap
args_parse(AllocatorPtr alloc, const std::string_view p) noexcept
{
	StringMap args;

	for (const auto s : IterableSplitString(p, '&')) {
		const auto [name, escaped_value] = Split(s, '=');
		if (name.empty() || escaped_value.data() == nullptr)
			continue;

		char *value = uri_unescape_dup(alloc, escaped_value,
					       ARGS_ESCAPE_CHAR);
		if (value != nullptr)
			args.Add(alloc, alloc.DupZ(name), value);
	}

	return args;
}

const char *
args_format_n(AllocatorPtr alloc, const StringMap *args,
	      const char *replace_key, std::string_view replace_value,
	      const char *replace_key2, std::string_view replace_value2,
	      const char *replace_key3, std::string_view replace_value3,
	      const char *remove_key) noexcept
{
	size_t length = 0;

	/* determine length */

	if (args != nullptr)
		for (const auto &i : *args)
			length += strlen(i.key) + 1 + strlen(i.value) * 3 + 1;

	if (replace_key != nullptr)
		length += strlen(replace_key) + 1 + replace_value.size() * 3 + 1;

	if (replace_key2 != nullptr)
		length += strlen(replace_key2) + 1 + replace_value2.size() * 3 + 1;

	if (replace_key3 != nullptr)
		length += strlen(replace_key3) + 1 + replace_value3.size() * 3 + 1;

	/* allocate memory, format it */

	char *p = alloc.NewArray<char>(length + 1);
	const char *const ret = p;

	if (args != nullptr) {
		for (const auto &i : *args) {
			if ((replace_key != nullptr && strcmp(i.key, replace_key) == 0) ||
			    (replace_key2 != nullptr && strcmp(i.key, replace_key2) == 0) ||
			    (replace_key3 != nullptr && strcmp(i.key, replace_key3) == 0) ||
			    (remove_key != nullptr && strcmp(i.key, remove_key) == 0))
				continue;
			if (p > ret)
				*p++ = '&';
			length = strlen(i.key);
			memcpy(p, i.key, length);
			p += length;
			*p++ = '=';
			p += UriEscape(p, i.value, ARGS_ESCAPE_CHAR);
		}
	}

	if (replace_key != nullptr) {
		if (p > ret)
			*p++ = '&';
		length = strlen(replace_key);
		memcpy(p, replace_key, length);
		p += length;
		*p++ = '=';
		p += UriEscape(p, replace_value, ARGS_ESCAPE_CHAR);
	}

	if (replace_key2 != nullptr) {
		if (p > ret)
			*p++ = '&';
		length = strlen(replace_key2);
		memcpy(p, replace_key2, length);
		p += length;
		*p++ = '=';
		p += UriEscape(p, replace_value2, ARGS_ESCAPE_CHAR);
	}

	if (replace_key3 != nullptr) {
		if (p > ret)
			*p++ = '&';
		length = strlen(replace_key3);
		memcpy(p, replace_key3, length);
		p += length;
		*p++ = '=';
		p += UriEscape(p, replace_value3, ARGS_ESCAPE_CHAR);
	}

	*p = 0;
	return ret;
}

const char *
args_format(AllocatorPtr alloc, const StringMap *args,
	    const char *replace_key, std::string_view replace_value,
	    const char *replace_key2, std::string_view replace_value2,
	    const char *remove_key) noexcept
{
	return args_format_n(alloc, args,
			     replace_key, replace_value,
			     replace_key2, replace_value2,
			     nullptr, {},
			     remove_key);
}
