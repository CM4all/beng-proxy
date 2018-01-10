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

#include "text_processor.hxx"
#include "strmap.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream_subst.hxx"
#include "widget/Widget.hxx"
#include "widget/Class.hxx"
#include "penv.hxx"
#include "pool.hxx"

#include <assert.h>

gcc_pure
static bool
text_processor_allowed_content_type(const char *content_type)
{
    assert(content_type != NULL);

    return strncmp(content_type, "text/", 5) == 0 ||
        strncmp(content_type, "application/json", 16) == 0 ||
        strncmp(content_type, "application/javascript", 22) == 0;
}

bool
text_processor_allowed(const StringMap &headers)
{
    const char *content_type = headers.Get("content-type");
    return content_type != NULL &&
        text_processor_allowed_content_type(content_type);
}

gcc_pure
static const char *
base_uri(struct pool *pool, const char *absolute_uri)
{
    const char *p;

    if (absolute_uri == NULL)
        return NULL;

    p = strchr(absolute_uri, ';');
    if (p == NULL) {
        p = strchr(absolute_uri, '?');
        if (p == NULL)
            return absolute_uri;
    }

    return p_strndup(pool, absolute_uri, p - absolute_uri);
}

static void
processor_subst_beng_widget(Istream &istream,
                            const Widget &widget,
                            const struct processor_env &env)
{
    istream_subst_add(istream, "&c:type;", widget.class_name);
    istream_subst_add(istream, "&c:class;", widget.GetQuotedClassName());
    istream_subst_add(istream, "&c:local;", widget.cls->local_uri);
    istream_subst_add(istream, "&c:id;", widget.id);
    istream_subst_add(istream, "&c:path;", widget.GetIdPath());
    istream_subst_add(istream, "&c:prefix;", widget.GetPrefix());
    istream_subst_add(istream, "&c:uri;", env.absolute_uri);
    istream_subst_add(istream, "&c:base;", base_uri(env.pool, env.uri));
    istream_subst_add(istream, "&c:frame;",
                      strmap_get_checked(env.args, "frame"));
    istream_subst_add(istream, "&c:view;", widget.GetEffectiveView()->name);
    istream_subst_add(istream, "&c:session;",
                      strmap_get_checked(env.args, "session"));
}

UnusedIstreamPtr
text_processor(struct pool &pool, UnusedIstreamPtr input,
               const Widget &widget, const struct processor_env &env)
{
    auto *subst = istream_subst_new(&pool, std::move(input));
    processor_subst_beng_widget(*subst, widget, env);
    return UnusedIstreamPtr(subst);
}
