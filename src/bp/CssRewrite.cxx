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
#include "css_parser.hxx"
#include "widget/RewriteUri.hxx"
#include "widget/Context.hxx"
#include "pool/pool.hxx"
#include "pool/tpool.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/istream.hxx"
#include "istream/istream_memory.hxx"
#include "istream/ReplaceIstream.hxx"
#include "util/StringView.hxx"

#include <assert.h>

struct css_url {
	size_t start, end;
};

struct css_rewrite {
	CssParser *parser;

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
	assert(rewrite->parser != nullptr);

	if (rewrite->n_urls < std::size(rewrite->urls)) {
		struct css_url *p = &rewrite->urls[rewrite->n_urls++];
		p->start = url->start;
		p->end = url->end;
	}
}

static void
css_rewrite_parser_eof(void *ctx, off_t length gcc_unused) noexcept
{
	struct css_rewrite *rewrite = (struct css_rewrite *)ctx;
	assert(rewrite->parser != nullptr);

	rewrite->parser = nullptr;
}

#ifndef NDEBUG
gcc_noreturn
#endif
static void
css_rewrite_parser_error(std::exception_ptr, void *ctx) noexcept
{
	struct css_rewrite *rewrite = (struct css_rewrite *)ctx;
	(void)rewrite;

	/* shouldn't happen - input is an istream_memory which never
	   fails */
	assert(false);
}

static constexpr CssParserHandler css_rewrite_parser_handler = {
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	css_rewrite_parser_url,
	nullptr,
	css_rewrite_parser_eof,
	css_rewrite_parser_error,
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

		rewrite.parser = css_parser_new(*tpool,
						istream_memory_new(*tpool, block.data,
								   block.size),
						true,
						css_rewrite_parser_handler, &rewrite);
		css_parser_read(rewrite.parser);
	}

	assert(rewrite.parser == nullptr);

	if (rewrite.n_urls == 0)
		/* no URLs found, no rewriting necessary */
		return nullptr;

	auto input =
		istream_memory_new(pool, p_strdup(pool, block), block.size);
	auto replace = istream_replace_new(ctx->event_loop, pool,
					   std::move(input));

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

		replace.second->Add(url->start, url->end, std::move(value));
		modified = true;
	}

	if (!modified)
		return nullptr;

	replace.second->Finish();
	return std::move(replace.first);
}
