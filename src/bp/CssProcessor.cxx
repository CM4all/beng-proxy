// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "CssProcessor.hxx"
#include "parser/CssParser.hxx"
#include "parser/CssUtil.hxx"
#include "widget/Widget.hxx"
#include "widget/RewriteUri.hxx"
#include "widget/Context.hxx"
#include "escape/CSS.hxx"
#include "istream/ReplaceIstream.hxx"
#include "istream/istream_string.hxx"
#include "pool/SharedPtr.hxx"
#include "pool/pool.hxx"
#include "util/StringAPI.hxx"
#include "stopwatch.hxx"

#include <assert.h>
#include <string.h>

struct CssProcessor final : public ReplaceIstream {
	const StopwatchPtr stopwatch;

	Widget &container;
	const SharedPoolPtr<WidgetContext> ctx;
	const unsigned options;

	CssParser parser;
	bool had_input;

	struct UriRewrite {
		RewriteUriMode mode;

		char view[64];
	};

	UriRewrite uri_rewrite;

	CssProcessor(PoolPtr &&_pool, const StopwatchPtr &parent_stopwatch,
		     UnusedIstreamPtr input,
		     Widget &_container,
		     SharedPoolPtr<WidgetContext> &&_ctx,
		     unsigned _options) noexcept;

	using ReplaceIstream::GetPool;

	/* virtual methods from class ReplaceIstream */
	void Parse(std::span<const std::byte> b) override {
		parser.Feed((const char *)b.data(), b.size());
	}

	void ParseEnd() override {
		ReplaceIstream::Finish();
	}
};

static inline bool
css_processor_option_rewrite_url(const CssProcessor *processor) noexcept
{
	return (processor->options & CSS_PROCESSOR_REWRITE_URL) != 0;
}

static inline bool
css_processor_option_prefix_class(const CssProcessor *processor) noexcept
{
	return (processor->options & CSS_PROCESSOR_PREFIX_CLASS) != 0;
}

static inline bool
css_processor_option_prefix_id(const CssProcessor *processor) noexcept
{
	return (processor->options & CSS_PROCESSOR_PREFIX_ID) != 0;
}

static void
css_processor_replace_add(ReplaceIstream *processor,
			  off_t start, off_t end,
			  UnusedIstreamPtr istream) noexcept
{
	processor->Add(start, end, std::move(istream));
}

/*
 * css_parser_handler
 *
 */

static void
css_processor_parser_class_name(const CssParserValue *name, void *ctx) noexcept
{
	CssProcessor *processor = (CssProcessor *)ctx;

	assert(!name->value.empty());

	if (!css_processor_option_prefix_class(processor))
		return;

	unsigned n = underscore_prefix(name->value);
	if (n == 3) {
		/* triple underscore: add widget path prefix */

		const char *prefix = processor->container.GetPrefix();
		if (prefix == nullptr)
			return;

		css_processor_replace_add(processor, name->start, name->start + 3,
					  istream_string_new(processor->GetPool(), prefix));
	} else if (n == 2) {
		/* double underscore: add class name prefix */

		const char *class_name = processor->container.GetQuotedClassName();
		if (class_name == nullptr)
			return;

		css_processor_replace_add(processor, name->start, name->start + 2,
					  istream_string_new(processor->GetPool(),
							     class_name));
	}
}

static void
css_processor_parser_xml_id(const CssParserValue *name, void *ctx) noexcept
{
	CssProcessor *processor = (CssProcessor *)ctx;

	assert(!name->value.empty());

	if (!css_processor_option_prefix_id(processor))
		return;

	unsigned n = underscore_prefix(name->value);
	if (n == 3) {
		/* triple underscore: add widget path prefix */

		const char *prefix = processor->container.GetPrefix();
		if (prefix == nullptr)
			return;

		css_processor_replace_add(processor, name->start, name->start + 3,
					  istream_string_new(processor->GetPool(),
							     prefix));
	} else if (n == 2) {
		/* double underscore: add class name prefix */

		const char *class_name = processor->container.GetQuotedClassName();
		if (class_name == nullptr)
			return;

		css_processor_replace_add(processor, name->start, name->start + 1,
					  istream_string_new(processor->GetPool(),
							     class_name));
	}
}

static void
css_processor_parser_block(void *ctx) noexcept
{
	CssProcessor *processor = (CssProcessor *)ctx;

	processor->uri_rewrite.mode = RewriteUriMode::PARTIAL;
	processor->uri_rewrite.view[0] = 0;
}

static void
css_processor_parser_property_keyword(const char *name, std::string_view value,
				      off_t start, off_t end,
				      void *ctx) noexcept
{
	CssProcessor *processor = (CssProcessor *)ctx;

	if (css_processor_option_rewrite_url(processor) &&
	    StringIsEqual(name, "-c-mode")) {
		processor->uri_rewrite.mode = parse_uri_mode(value);
		css_processor_replace_add(processor, start, end, nullptr);
	}

	if (css_processor_option_rewrite_url(processor) &&
	    StringIsEqual(name, "-c-view") &&
	    value.size() < sizeof(processor->uri_rewrite.view)) {
		*std::copy(value.begin(), value.end(),
			   processor->uri_rewrite.view) = 0;
		css_processor_replace_add(processor, start, end, nullptr);
	}
}

static void
css_processor_parser_url(const CssParserValue *url, void *ctx) noexcept
{
	CssProcessor *processor = (CssProcessor *)ctx;

	if (!css_processor_option_rewrite_url(processor) ||
	    processor->container.IsRoot())
		return;

	auto istream =
		rewrite_widget_uri(processor->GetPool(),
				   processor->ctx,
				   processor->stopwatch,
				   processor->container,
				   url->value,
				   processor->uri_rewrite.mode, false,
				   processor->uri_rewrite.view[0] != 0
				   ? processor->uri_rewrite.view : nullptr,
				   &css_escape_class);
	if (istream)
		css_processor_replace_add(processor, url->start, url->end,
					  std::move(istream));
}

static void
css_processor_parser_import(const CssParserValue *url, void *ctx) noexcept
{
	CssProcessor *processor = (CssProcessor *)ctx;

	if (!css_processor_option_rewrite_url(processor) ||
	    processor->container.IsRoot())
		return;

	auto istream =
		rewrite_widget_uri(processor->GetPool(),
				   processor->ctx,
				   processor->stopwatch,
				   processor->container,
				   url->value,
				   RewriteUriMode::PARTIAL, false, nullptr,
				   &css_escape_class);
	if (istream)
		css_processor_replace_add(processor, url->start, url->end,
					  std::move(istream));
}

static constexpr CssParserHandler css_processor_parser_handler = {
	css_processor_parser_class_name,
	css_processor_parser_xml_id,
	css_processor_parser_block,
	css_processor_parser_property_keyword,
	css_processor_parser_url,
	css_processor_parser_import,
};

/*
 * constructor
 *
 */

inline
CssProcessor::CssProcessor(PoolPtr &&_pool,
			   const StopwatchPtr &parent_stopwatch,
			   UnusedIstreamPtr _input,
			   Widget &_container,
			   SharedPoolPtr<WidgetContext> &&_ctx,
			   unsigned _options) noexcept
	:ReplaceIstream(std::move(_pool), _ctx->event_loop, std::move(_input)),
	 stopwatch(parent_stopwatch, "CssProcessor"),
	 container(_container), ctx(std::move(_ctx)),
	 options(_options),
	 parser(false, css_processor_parser_handler, this)
{
}

UnusedIstreamPtr
css_processor(struct pool &caller_pool,
	      const StopwatchPtr &parent_stopwatch,
	      UnusedIstreamPtr input,
	      Widget &widget,
	      SharedPoolPtr<WidgetContext> ctx,
	      unsigned options) noexcept
{
	auto pool = pool_new_linear(&caller_pool, "css_processor", 32768);

	auto *processor =
		NewFromPool<CssProcessor>(std::move(pool), parent_stopwatch,
					  std::move(input),
					  widget, std::move(ctx),
					  options);
	return UnusedIstreamPtr(processor);
}
