// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Pool.hxx"
#include "Class.hxx"
#include "AllocatorPtr.hxx"

#include <assert.h>

const char *
escape_dup(AllocatorPtr alloc, const struct escape_class &cls,
	   std::string_view p) noexcept
{
	assert(cls.escape_size != nullptr);
	assert(cls.escape != nullptr);

	size_t size = cls.escape_size(p);
	if (size == 0)
		return alloc.DupZ(p);

	char *q = alloc.NewArray<char>(size + 1);
	size_t out_size = cls.escape(p, q);
	assert(out_size <= size);
	q[out_size] = 0;

	return q;
}

std::string_view
unescape_dup(AllocatorPtr alloc, const struct escape_class &cls,
	     std::string_view src) noexcept
{
	char *unescaped = alloc.NewArray<char>(src.size());

	return {
		unescaped,
		unescape_buffer(&cls, src, unescaped),
	};
}
