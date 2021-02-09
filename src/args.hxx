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

#pragma once

class AllocatorPtr;
class StringMap;
struct StringView;

/**
 * Parse the argument list in an URI after the semicolon.
 */
[[gnu::pure]]
StringMap
args_parse(AllocatorPtr alloc, StringView p) noexcept;

/**
 * Format the arguments into a string in the form
 * "KEY=VALUE&KEY2=VALUE2&...".
 *
 * @param replace_key add, replace or remove an entry in the args map
 * @param replace_value the new value or nullptr if the key should be removed
 */
[[gnu::pure]]
const char *
args_format_n(AllocatorPtr alloc, const StringMap *args,
	      const char *replace_key, StringView replace_value,
	      const char *replace_key2, StringView replace_value2,
	      const char *replace_key3, StringView replace_value3,
	      const char *remove_key) noexcept;

[[gnu::pure]]
const char *
args_format(AllocatorPtr alloc, const StringMap *args,
	    const char *replace_key, StringView replace_value,
	    const char *replace_key2, StringView replace_value2,
	    const char *remove_key) noexcept;
