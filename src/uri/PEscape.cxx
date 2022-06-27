/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "PEscape.hxx"
#include "uri/Escape.hxx"
#include "uri/Unescape.hxx"
#include "AllocatorPtr.hxx"

#include <algorithm>

const char *
uri_escape_dup(AllocatorPtr alloc, std::string_view src,
	       char escape_char)
{
	char *dest = alloc.NewArray<char>(src.size() * 3 + 1);
	size_t dest_length = UriEscape(dest, src, escape_char);
	dest[dest_length] = 0;
	return dest;
}

char *
uri_unescape_dup(AllocatorPtr alloc, std::string_view src,
		 char escape_char)
{
	char *dest = alloc.NewArray<char>(src.size() + 1);
	char *end = UriUnescape(dest, src, escape_char);
	if (end == nullptr)
		return nullptr;

	*end = 0;
	return dest;
}

char *
uri_unescape_concat(AllocatorPtr alloc, std::string_view uri,
		    std::string_view escaped_tail) noexcept
{
	/* worst-case allocation */
	char *dest = alloc.NewArray<char>(uri.size() + escaped_tail.size() + 1);

	/* first copy "uri" */
	char *p = std::copy(uri.begin(), uri.end(), dest);

	/* append "escaped_tail", and fail this function if unescaping
	   fails */
	p = UriUnescape(p, escaped_tail);
	if (p == nullptr)
		return nullptr;

	*p = 0;

	return dest;
}
