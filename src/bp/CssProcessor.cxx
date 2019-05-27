/*
 * Copyright 2007-2019 Content Management AG
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

#include "CssProcessor.hxx"
#include "css_parser.hxx"
#include "css_util.hxx"
#include "Global.hxx"
#include "penv.hxx"
#include "strmap.hxx"
#include "widget/Widget.hxx"
#include "widget/RewriteUri.hxx"
#include "pool/Holder.hxx"
#include "pool/tpool.hxx"
#include "escape_css.hxx"
#include "istream/istream.hxx"
#include "istream/ReplaceIstream.hxx"
#include "istream/istream_string.hxx"
#include "istream/TeeIstream.hxx"
#include "pool/pool.hxx"

#include <assert.h>
#include <string.h>

struct CssProcessor final : PoolHolder {
    Widget &container;
    struct processor_env &env;
    const unsigned options;

    SharedPoolPtr<ReplaceIstreamControl> replace;

    CssParser *parser;
    bool had_input;

    struct UriRewrite {
        RewriteUriMode mode;

        char view[64];
    };

    UriRewrite uri_rewrite;

    CssProcessor(PoolPtr &&_pool,
                 UnusedIstreamPtr input,
                 SharedPoolPtr<ReplaceIstreamControl> _replace,
                 Widget &_container,
                 struct processor_env &_env,
                 unsigned _options);

    void Destroy() noexcept {
        this->~CssProcessor();
    }

    using PoolHolder::GetPool;
};

static inline bool
css_processor_option_rewrite_url(const CssProcessor *processor)
{
    return (processor->options & CSS_PROCESSOR_REWRITE_URL) != 0;
}

static inline bool
css_processor_option_prefix_class(const CssProcessor *processor)
{
    return (processor->options & CSS_PROCESSOR_PREFIX_CLASS) != 0;
}

static inline bool
css_processor_option_prefix_id(const CssProcessor *processor)
{
    return (processor->options & CSS_PROCESSOR_PREFIX_ID) != 0;
}

static void
css_processor_replace_add(CssProcessor *processor,
                          off_t start, off_t end,
                          UnusedIstreamPtr istream)
{
    processor->replace->Add(start, end, std::move(istream));
}

/*
 * css_parser_handler
 *
 */

static void
css_processor_parser_class_name(const CssParserValue *name, void *ctx)
{
    CssProcessor *processor = (CssProcessor *)ctx;

    assert(!name->value.empty());

    if (!css_processor_option_prefix_class(processor))
        return;

    unsigned n = underscore_prefix(name->value.begin(), name->value.end());
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
css_processor_parser_xml_id(const CssParserValue *name, void *ctx)
{
    CssProcessor *processor = (CssProcessor *)ctx;

    assert(!name->value.empty());

    if (!css_processor_option_prefix_id(processor))
        return;

    unsigned n = underscore_prefix(name->value.begin(), name->value.end());
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
css_processor_parser_block(void *ctx)
{
    CssProcessor *processor = (CssProcessor *)ctx;

    processor->uri_rewrite.mode = RewriteUriMode::PARTIAL;
    processor->uri_rewrite.view[0] = 0;
}

static void
css_processor_parser_property_keyword(const char *name, StringView value,
                                      off_t start, off_t end, void *ctx)
{
    CssProcessor *processor = (CssProcessor *)ctx;

    if (css_processor_option_rewrite_url(processor) &&
        strcmp(name, "-c-mode") == 0) {
        processor->uri_rewrite.mode = parse_uri_mode(value);
        css_processor_replace_add(processor, start, end, nullptr);
    }

    if (css_processor_option_rewrite_url(processor) &&
        strcmp(name, "-c-view") == 0 &&
        value.size < sizeof(processor->uri_rewrite.view)) {
        memcpy(processor->uri_rewrite.view, value.data, value.size);
        processor->uri_rewrite.view[value.size] = 0;
        css_processor_replace_add(processor, start, end, nullptr);
    }
}

static void
css_processor_parser_url(const CssParserValue *url, void *ctx)
{
    CssProcessor *processor = (CssProcessor *)ctx;

    if (!css_processor_option_rewrite_url(processor))
        return;

    auto istream =
        rewrite_widget_uri(processor->GetPool(),
                           processor->env,
                           *global_translation_service,
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
css_processor_parser_import(const CssParserValue *url, void *ctx)
{
    CssProcessor *processor = (CssProcessor *)ctx;

    if (!css_processor_option_rewrite_url(processor))
        return;

    auto istream =
        rewrite_widget_uri(processor->GetPool(),
                           processor->env,
                           *global_translation_service,
                           processor->container,
                           url->value,
                           RewriteUriMode::PARTIAL, false, nullptr,
                           &css_escape_class);
    if (istream)
        css_processor_replace_add(processor, url->start, url->end,
                                  std::move(istream));
}

static void
css_processor_parser_eof(void *ctx, off_t length gcc_unused)
{
    CssProcessor *processor = (CssProcessor *)ctx;

    assert(processor->parser != nullptr);

    processor->parser = nullptr;

    processor->replace->Finish();
    processor->Destroy();
}

static void
css_processor_parser_error(std::exception_ptr, void *ctx)
{
    CssProcessor *processor = (CssProcessor *)ctx;

    assert(processor->parser != nullptr);

    processor->parser = nullptr;

    processor->Destroy();
}

static constexpr CssParserHandler css_processor_parser_handler = {
    .class_name = css_processor_parser_class_name,
    .xml_id = css_processor_parser_xml_id,
    .block = css_processor_parser_block,
    .property_keyword = css_processor_parser_property_keyword,
    .url = css_processor_parser_url,
    .import = css_processor_parser_import,
    .eof = css_processor_parser_eof,
    .error = css_processor_parser_error,
};

/*
 * constructor
 *
 */

inline
CssProcessor::CssProcessor(PoolPtr &&_pool,
                           UnusedIstreamPtr input,
                           SharedPoolPtr<ReplaceIstreamControl> _replace,
                           Widget &_container,
                           struct processor_env &_env,
                           unsigned _options)
    :PoolHolder(std::move(_pool)),
     container(_container), env(_env),
     options(_options),
     replace(std::move(_replace)),
     parser(css_parser_new(pool, std::move(input), false,
                           css_processor_parser_handler, this)) {}

UnusedIstreamPtr
css_processor(struct pool &caller_pool, UnusedIstreamPtr input,
              Widget &widget,
              struct processor_env &env,
              unsigned options)
{
    auto pool = pool_new_linear(&caller_pool, "css_processor", 32768);

    auto tee = istream_tee_new(pool, std::move(input),
                               *env.event_loop,
                               true, true);

    auto replace = istream_replace_new(*env.event_loop, pool,
                                       std::move(tee.second));

    NewFromPool<CssProcessor>(std::move(pool),
                              std::move(tee.first),
                              std::move(replace.second),
                              widget, env,
                              options);

    return std::move(replace.first);
}
