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

#include "Dissect.hxx"
#include "Verify.hxx"

#include <string.h>

bool
DissectedUri::Parse(const char *src)
{
    const char *qmark = strchr(src, '?');

    const char *semicolon;
    if (qmark == nullptr)
        semicolon = strchr(src, ';');
    else
        semicolon = (const char *)memchr(src, ';', qmark - src);

    base.data = src;
    if (semicolon != nullptr)
        base.size = semicolon - src;
    else if (qmark != nullptr)
        base.size = qmark - src;
    else
        base.size = strlen(src);

    if (!uri_path_verify(base))
        return false;

    if (semicolon == nullptr) {
        args = nullptr;
        path_info = nullptr;
    } else {
        /* XXX second semicolon for stuff being forwared? */
        args.data = semicolon + 1;
        if (qmark == nullptr)
            args.size = strlen(args.data);
        else
            args.size = qmark - args.data;

        const char *slash = args.Find('/');
        if (slash != nullptr) {
            path_info.data = slash;
            path_info.size = args.end() - slash;
            args.size = slash - args.data;
        } else
            path_info = nullptr;
    }

    if (qmark == nullptr)
        query = nullptr;
    else
        query = { qmark + 1, strlen(qmark + 1) };

    return true;
}
