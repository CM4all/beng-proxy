/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "pool.hxx"
#include "util/CharUtil.hxx"
#include "util/StringView.hxx"

#include <algorithm>

#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

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
p_strdup_lower(struct pool *pool, const char *src
	       TRACE_ARGS_DECL) noexcept
{
	return p_strndup_lower_fwd(pool, src, strlen(src));
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
p_strndup_lower(struct pool *pool, const char *src, size_t length
		TRACE_ARGS_DECL) noexcept
{
	char *dest = (char *)p_malloc_fwd(pool, length + 1);
	*CopyLower(dest, src, length) = 0;
	return dest;
}

char *
p_strdup(struct pool &pool, StringView src TRACE_ARGS_DECL) noexcept
{
	char *dest = (char *)p_malloc_fwd(&pool, src.size + 1);
	*Copy(dest, src.data, src.size) = 0;
	return dest;
}

char *
p_strdup_lower(struct pool &pool, StringView src TRACE_ARGS_DECL) noexcept
{
	char *dest = (char *)p_malloc_fwd(&pool, src.size + 1);
	*CopyLower(dest, src.data, src.size) = 0;
	return dest;
}

char * gcc_malloc
p_sprintf(struct pool *pool, const char *fmt, ...) noexcept
{
	va_list ap;
	va_start(ap, fmt);
	size_t length = (size_t)vsnprintf(nullptr, 0, fmt, ap) + 1;
	va_end(ap);

	char *p = (char *)p_malloc(pool, length);

	va_start(ap, fmt);
	gcc_unused int length2 = vsnprintf(p, length, fmt, ap);
	va_end(ap);

	assert((size_t)length2 + 1 == length);

	return p;
}
