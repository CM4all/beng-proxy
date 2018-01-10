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
 * Rewrite URIs in templates.
 */

#ifndef __BENG_REWRITE_URI_H
#define __BENG_REWRITE_URI_H

#include "util/Compiler.h"

struct pool;
class Istream;
struct tcache;
struct parsed_uri;
class StringMap;
struct Widget;
struct StringView;
struct escape_class;

enum class RewriteUriMode {
    DIRECT,
    FOCUS,
    PARTIAL,

    /**
     * Embed the widget's HTTP response instead of generating an URI
     * to the widget server.
     */
    RESPONSE,
};

gcc_pure
RewriteUriMode
parse_uri_mode(StringView s) noexcept;

/**
 * @param untrusted_host the value of the UNTRUSTED translation
 * packet, or NULL if this is a "trusted" request
 * @param stateful if true, then the current request/session state is
 * taken into account (path_info and query_string)
 * @param view the name of a view, or NULL to use the default view
 */
gcc_malloc
Istream *
rewrite_widget_uri(struct pool &pool,
                   struct processor_env &env,
                   struct tcache &translate_cache,
                   Widget &widget,
                   StringView value,
                   RewriteUriMode mode, bool stateful,
                   const char *view,
                   const struct escape_class *escape) noexcept;

#endif
