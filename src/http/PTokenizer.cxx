/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "PTokenizer.hxx"
#include "Tokenizer.hxx"
#include "Chars.hxx"
#include "util/StringView.hxx"
#include "AllocatorPtr.hxx"

void
http_next_quoted_string(AllocatorPtr alloc, StringView &input,
			StringView &value) noexcept
{
	char *dest = alloc.NewArray<char>(input.size); /* TODO: optimize memory consumption */
	size_t pos = 1;

	value.size = 0;
	value.data = dest;

	while (pos < input.size) {
		if (input[pos] == '\\') {
			++pos;
			if (pos < input.size)
				dest[value.size++] = input[pos++];
		} else if (input[pos] == '"') {
			++pos;
			break;
		} else if (char_is_http_text(input[pos])) {
			dest[value.size++] = input[pos++];
		} else {
			++pos;
		}
	}

	input.skip_front(pos);
}

void
http_next_value(AllocatorPtr alloc,
		StringView &input, StringView &value) noexcept
{
	if (!input.empty() && input.front() == '"')
		http_next_quoted_string(alloc, input, value);
	else
		http_next_token(input, value);
}

void
http_next_name_value(AllocatorPtr alloc, StringView &input,
		     StringView &name, StringView &value) noexcept
{
	http_next_token(input, name);
	if (name.empty())
		return;

	input.StripLeft();
	if (!input.empty() && input.front() == '=') {
		input.pop_front();
		input.StripLeft();

		http_next_value(alloc, input, value);
	} else
		value = nullptr;
}
