/*
 * Copyright (C) 2009-2015 Max Kellermann <max@duempel.org>
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

#ifndef STRING_UTIL_HXX
#define STRING_UTIL_HXX

#include <inline/compiler.h>

#include <stddef.h>

/**
 * Skips whitespace at the beginning of the string, and returns the
 * first non-whitespace character.  If the string has no
 * non-whitespace characters, then a pointer to the NULL terminator is
 * returned.
 */
gcc_pure gcc_nonnull_all
const char *
StripLeft(const char *p);

gcc_pure gcc_nonnull_all
static inline char *
StripLeft(char *p)
{
	return const_cast<char *>(StripLeft((const char *)p));
}

/**
 * Skips whitespace at the beginning of the string, and returns the
 * first non-whitespace character or the end pointer.
 */
gcc_pure
const char *
StripLeft(const char *p, const char *end);

/**
 * Determine the string's end as if it was stripped on the right side.
 */
gcc_pure
const char *
StripRight(const char *p, const char *end);

/**
 * Determine the string's end as if it was stripped on the right side.
 */
gcc_pure
static inline char *
StripRight(char *p, char *end)
{
	return const_cast<char *>(StripRight((const char *)p,
					     (const char *)end));
}

/**
 * Determine the string's length as if it was stripped on the right
 * side.
 */
gcc_pure
size_t
StripRight(const char *p, size_t length);

/**
 * Strips trailing whitespace.
 */
gcc_nonnull_all
void
StripRight(char *p);

#endif
