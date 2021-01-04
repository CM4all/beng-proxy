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

#include "TextProcessor.hxx"
#include "strmap.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/SubstIstream.hxx"
#include "widget/Widget.hxx"
#include "widget/Class.hxx"
#include "widget/Context.hxx"
#include "pool/pool.hxx"
#include "util/CharUtil.hxx"
#include "util/HexFormat.h"

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

static constexpr bool
MustEscape(char ch) noexcept
{
	/* escape all characters which may be dangerous inside HTML */
	/* note: we don't escape '%' because we assume that the input
	   value has already been escaped, and this isn't about
	   protecting URIs, but about protecting HTML and
	   JavaScript from injection attacks */
	return ch == '\'' || ch == '"' || ch == '&' ||
		ch == '<' || ch == '>' ||
		!IsPrintableASCII(ch);
}

static size_t
CountMustEscape(StringView s) noexcept
{
	size_t n = 0;
	for (char ch : s)
		if (MustEscape(ch))
			++n;
	return n;
}

static StringView
EscapeValue(struct pool &pool, StringView v) noexcept
{
	const size_t n_escape = CountMustEscape(v);
	if (n_escape == 0)
		return v;

	const size_t result_length = v.size + n_escape * 2;
	char *p = PoolAlloc<char>(pool, result_length);
	const StringView result(p, result_length);

	for (char ch : v) {
		if (MustEscape(ch)) {
			*p++ = '%';
			format_uint8_hex_fixed(p, ch);
			p += 2;
		} else
			*p++ = ch;
	}

	return result;
}

static SubstTree
processor_subst_beng_widget(struct pool &pool,
			    const Widget &widget,
			    const WidgetContext &ctx)
{
	SubstTree subst;
	subst.Add(pool, "&c:type;", widget.class_name);
	subst.Add(pool, "&c:class;", widget.GetQuotedClassName());
	subst.Add(pool, "&c:local;", widget.cls->local_uri);
	subst.Add(pool, "&c:id;", widget.id);
	subst.Add(pool, "&c:path;", widget.GetIdPath());
	subst.Add(pool, "&c:prefix;", widget.GetPrefix());
	subst.Add(pool, "&c:uri;", EscapeValue(pool, ctx.absolute_uri));
	subst.Add(pool, "&c:base;", EscapeValue(pool, base_uri(&pool, ctx.uri)));
	subst.Add(pool, "&c:frame;", EscapeValue(pool, strmap_get_checked(ctx.args, "frame")));
	subst.Add(pool, "&c:view;", widget.GetEffectiveView()->name);
	subst.Add(pool, "&c:session;", nullptr); /* obsolete as of version 15.29 */
	return subst;
}

UnusedIstreamPtr
text_processor(struct pool &pool, UnusedIstreamPtr input,
	       const Widget &widget, const WidgetContext &ctx)
{
	return istream_subst_new(&pool, std::move(input),
				 processor_subst_beng_widget(pool, widget, ctx));
}
