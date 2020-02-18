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

#include "Quote.hxx"
#include "Chars.hxx"
#include "util/StringView.hxx"

bool
http_must_quote_token(StringView src) noexcept
{
	for (auto ch : src)
		if (!char_is_http_token(ch))
			return true;
	return false;
}

size_t
http_quote_string(char *dest, const StringView src) noexcept
{
	size_t dest_pos = 0, src_pos = 0;

	dest[dest_pos++] = '"';

	while (src_pos < src.size) {
		if (src[src_pos] == '"' || src[src_pos] == '\\') {
			dest[dest_pos++] = '\\';
			dest[dest_pos++] = src[src_pos++];
		} else if (char_is_http_text(src[src_pos]))
			dest[dest_pos++] = src[src_pos++];
		else
			/* ignore invalid characters */
			++src_pos;
	}

	dest[dest_pos++] = '"';
	return dest_pos;
}
