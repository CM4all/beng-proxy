// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "CssRewrite.hxx"
#include "parser/CssParser.hxx"
#include "widget/RewriteUri.hxx"
#include "widget/Context.hxx"
#include "pool/pool.hxx"
#include "pool/tpool.hxx"
#include "pool/SharedPtr.hxx"
#include "istream/New.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream_string.hxx"
#include "istream/ReplaceIstream.hxx"
#include "AllocatorPtr.hxx"

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
		       const std::string_view block,
		       const struct escape_class *escape) noexcept
{
	struct css_rewrite rewrite;

	{
		const TempPoolLease tpool;

		CssParser parser(true, css_rewrite_parser_handler, &rewrite);
		parser.Feed(block.data(), block.size());
	}

	if (rewrite.n_urls == 0)
		/* no URLs found, no rewriting necessary */
		return nullptr;

	const AllocatorPtr alloc{pool};
	auto replace = NewIstream<ReplaceIstream>(pool, ctx->event_loop,
						  istream_string_new(pool, alloc.Dup(block)));

	bool modified = false;
	for (unsigned i = 0; i < rewrite.n_urls; ++i) {
		const struct css_url *url = &rewrite.urls[i];

		auto value =
			rewrite_widget_uri(pool, ctx, parent_stopwatch,
					   widget,
					   {block.data() + url->start, url->end - url->start},
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
