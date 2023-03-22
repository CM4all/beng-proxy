// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "TextProcessor.hxx"
#include "ClassifyMimeType.hxx"
#include "strmap.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/SubstIstream.hxx"
#include "widget/Widget.hxx"
#include "widget/Class.hxx"
#include "widget/Context.hxx"
#include "pool/pool.hxx"
#include "util/CharUtil.hxx"
#include "util/HexFormat.hxx"
#include "util/StringCompare.hxx"

#include <assert.h>
#include <string.h>

using std::string_view_literals::operator""sv;

bool
text_processor_allowed(const StringMap &headers) noexcept
{
	const char *content_type = headers.Get("content-type");
	return content_type != nullptr && IsTextMimeType(content_type);
}

[[gnu::pure]]
static const char *
base_uri(struct pool *pool, const char *absolute_uri) noexcept
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
CountMustEscape(std::string_view s) noexcept
{
	size_t n = 0;
	for (char ch : s)
		if (MustEscape(ch))
			++n;
	return n;
}

static std::string_view
EscapeValue(struct pool &pool, std::string_view v) noexcept
{
	const size_t n_escape = CountMustEscape(v);
	if (n_escape == 0)
		return v;

	const size_t result_length = v.size() + n_escape * 2;
	char *p = PoolAlloc<char>(pool, result_length);
	const std::string_view result{p, result_length};

	for (char ch : v) {
		if (MustEscape(ch)) {
			*p++ = '%';
			p = HexFormatUint8Fixed(p, ch);
		} else
			*p++ = ch;
	}

	return result;
}

static std::string_view
EscapeValue(struct pool &pool, const char *v) noexcept
{
	return EscapeValue(pool,
			   v != nullptr
			   ? std::string_view{v}
			   : std::string_view{});
}

static SubstTree
processor_subst_beng_widget(struct pool &pool,
			    const Widget &widget,
			    const WidgetContext &ctx) noexcept
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
	       const Widget &widget, const WidgetContext &ctx) noexcept
{
	return istream_subst_new(&pool, std::move(input),
				 processor_subst_beng_widget(pool, widget, ctx));
}
