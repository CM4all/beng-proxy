// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "pool.hxx"
#include "util/CharUtil.hxx"

#include <algorithm>

#include <assert.h>
#include <string.h>
#include <stdarg.h>

static char *
Copy(char *dest, const char *src, size_t n) noexcept
{
	return std::copy_n(src, n, dest);
}

static char *
CopyLower(char *dest, const char *src, size_t n) noexcept
{
	return std::transform(src, src + n, dest, ToLowerASCII);
}

void *
p_memdup(struct pool *pool, const void *src, size_t length
	 TRACE_ARGS_DECL) noexcept
{
	void *dest = p_malloc_fwd(pool, length);
	memcpy(dest, src, length);
	return dest;
}

char *
p_strdup(struct pool *pool, const char *src
	 TRACE_ARGS_DECL) noexcept
{
	return (char *)p_memdup_fwd(pool, src, strlen(src) + 1);
}

char *
p_strndup(struct pool *pool, const char *src, size_t length
	  TRACE_ARGS_DECL) noexcept
{
	char *dest = (char *)p_malloc_fwd(pool, length + 1);
	*Copy(dest, src, length) = 0;
	return dest;
}

char *
p_strdup(struct pool &pool, std::string_view src TRACE_ARGS_DECL) noexcept
{
	char *dest = (char *)p_malloc_fwd(&pool, src.size() + 1);
	*Copy(dest, src.data(), src.size()) = 0;
	return dest;
}

char *
p_strdup_lower(struct pool &pool, std::string_view src TRACE_ARGS_DECL) noexcept
{
	char *dest = (char *)p_malloc_fwd(&pool, src.size() + 1);
	*CopyLower(dest, src.data(), src.size()) = 0;
	return dest;
}
