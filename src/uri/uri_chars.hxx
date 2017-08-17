/*
 * Copyright 2007-2017 Content Management AG
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

/*
 * URI character classification according to RFC 2396.
 */

#ifndef URI_CHARS_HXX
#define URI_CHARS_HXX

#include "util/CharUtil.hxx"

static constexpr inline bool
char_is_uri_mark(char ch)
{
    return ch == '-' || ch == '_' || ch == '.' || ch == '!' || ch == '~' ||
        ch == '*' || ch == '\'' || ch == '(' || ch == ')';
}

/**
 * See RFC 2396 2.3.
 */
static constexpr inline bool
char_is_uri_unreserved(char ch)
{
    return IsAlphaNumericASCII(ch) || char_is_uri_mark(ch);
}

/**
 * See RFC 2396 3.3.
 */
static constexpr inline bool
char_is_uri_pchar(char ch)
{
    return char_is_uri_unreserved(ch) ||
        ch == '%' || /* "escaped" */
        ch == ':' || ch == '@' || ch == '&' || ch == '=' || ch == '+' ||
        ch == '$' || ch == ',';
}

#endif
