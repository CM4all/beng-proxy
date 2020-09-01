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

/*
 * Verify URI parts.
 */

#pragma once

#include "util/Compiler.h"

struct StringView;

/**
 * Verifies one path segment of an URI according to RFC 2396.
 */
gcc_pure
bool
uri_segment_verify(StringView segment) noexcept;

/**
 * Verifies the path portion of an URI according to RFC 2396.
 */
gcc_pure
bool
uri_path_verify(StringView uri) noexcept;

/**
 * Performs some paranoid checks on the URI; the following is not
 * allowed:
 *
 * - %00
 * - %2f (encoded slash)
 * - "/../", "/./"
 * - "/..", "/." at the end
 *
 * It is assumed that the URI was already verified with
 * uri_path_verify().
 */
gcc_pure
bool
uri_path_verify_paranoid(const char *uri) noexcept;

/**
 * Quickly verify the validity of an URI (path plus query).  This may
 * be used before passing it to another server, not to be parsed by
 * this process.
 */
gcc_pure
bool
uri_path_verify_quick(const char *uri) noexcept;
