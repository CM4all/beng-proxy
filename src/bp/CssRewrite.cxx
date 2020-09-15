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

#include "CssRewrite.hxx"
#include "parser/CssParser.hxx"
#include "widget/RewriteUri.hxx"
#include "widget/Context.hxx"
#include "pool/pool.hxx"
#include "pool/tpool.hxx"
#include "pool/SharedPtr.hxx"
#include "istream/New.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream_memory.hxx"
#include "istream/ReplaceIstream.hxx"
#include "util/StringView.hxx"

struct css_url {
	size_t start, end;
};

struct css_rewrite {
	unsigned n_urls = 0;
	struct css_url urls[16];
};

/*
 * css_parser_handler
 *
 */

static void
css_rewrite_parser_url(const CssParserValue *url, void *ctx) noexcept
{
	struct css_rewrite *rewrite = (struct css_rewrite *)ctx;

	if (rewrite->n_urls < std::size(rewrite->urls)) {
		struct css_url *p = &rewrite->urls[rewrite->n_urls++];
		p->start = url->start;
		p->end = url->end;
	}
}

static constexpr CssParserHandler css_rewrite_parser_handler = {
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	css_rewrite_parser_url,
	nullptr,
};

/*
 * constructor
 *
 */

UnusedIstreamPtr
css_rewrite_block_uris(struct pool &pool,
		       SharedPoolPtr<WidgetContext> ctx,
		       const StopwatchPtr &parent_stopwatch,
		       Widget &widget,
		       const StringView block,
		       const struct escape_class *escape) noexcept
{
	struct css_rewrite rewrite;

	{
		const TempPoolLease tpool;

		CssParser parser(true, css_rewrite_parser_handler, &rewrite);
		parser.Feed(block.data, block.size);
	}

	if (rewrite.n_urls == 0)
		/* no URLs found, no rewriting necessary */
		return nullptr;

	auto input =
		istream_memory_new(pool, p_strdup(pool, block), block.size);
	auto replace = NewIstream<ReplaceIstream>(pool, ctx->event_loop, std::move(input));

	bool modified = false;
	for (unsigned i = 0; i < rewrite.n_urls; ++i) {
		const struct css_url *url = &rewrite.urls[i];

		auto value =
			rewrite_widget_uri(pool, ctx, parent_stopwatch,
					   widget,
					   {block.data + url->start, url->end - url->start},
					   RewriteUriMode::PARTIAL, false, nullptr,
					   escape);
		if (!value)
			continue;

		replace->Add(url->start, url->end, std::move(value));
		modified = true;
	}

	if (!modified)
		return nullptr;

	replace->Finish();
	return UnusedIstreamPtr(replace);
}
