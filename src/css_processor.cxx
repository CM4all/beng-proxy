/*
 * Process URLs in a CSS stream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "css_processor.hxx"
#include "css_parser.hxx"
#include "css_util.hxx"
#include "penv.hxx"
#include "strmap.hxx"
#include "widget/Widget.hxx"
#include "widget/RewriteUri.hxx"
#include "tpool.hxx"
#include "bp_global.hxx"
#include "escape_css.hxx"
#include "istream/istream.hxx"
#include "istream/istream_replace.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream_tee.hxx"
#include "pool.hxx"

#include <daemon/log.h>

#include <glib.h>

#include <assert.h>
#include <string.h>

struct CssProcessor {
    struct pool *pool, *caller_pool;

    Widget *container;
    struct processor_env *env;
    unsigned options;

    Istream *replace;

    CssParser *parser;
    bool had_input;

    struct UriRewrite {
        enum uri_mode mode;

        char view[64];
    };

    UriRewrite uri_rewrite;

    CssProcessor(struct pool &_pool, struct pool &_caller_pool, Istream &tee,
                 Widget &_container,
                 struct processor_env &_env,
                 unsigned _options);
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
                          Istream *istream)
{
    istream_replace_add(*processor->replace, start, end, istream);
}

/*
 * css_parser_handler
 *
 */

static void
css_processor_parser_class_name(const CssParserValue *name, void *ctx)
{
    CssProcessor *processor = (CssProcessor *)ctx;

    assert(!name->value.IsEmpty());

    if (!css_processor_option_prefix_class(processor))
        return;

    unsigned n = underscore_prefix(name->value.begin(), name->value.end());
    if (n == 3) {
        /* triple underscore: add widget path prefix */

        const char *prefix = processor->container->GetPrefix();
        if (prefix == nullptr)
            return;

        css_processor_replace_add(processor, name->start, name->start + 3,
                                  istream_string_new(processor->pool,
                                                     prefix));
    } else if (n == 2) {
        /* double underscore: add class name prefix */

        const char *class_name = processor->container->GetQuotedClassName();
        if (class_name == nullptr)
            return;

        css_processor_replace_add(processor, name->start, name->start + 2,
                                  istream_string_new(processor->pool,
                                                     class_name));
    }
}

static void
css_processor_parser_xml_id(const CssParserValue *name, void *ctx)
{
    CssProcessor *processor = (CssProcessor *)ctx;

    assert(!name->value.IsEmpty());

    if (!css_processor_option_prefix_id(processor))
        return;

    unsigned n = underscore_prefix(name->value.begin(), name->value.end());
    if (n == 3) {
        /* triple underscore: add widget path prefix */

        const char *prefix = processor->container->GetPrefix();
        if (prefix == nullptr)
            return;

        css_processor_replace_add(processor, name->start, name->start + 3,
                                  istream_string_new(processor->pool,
                                                     prefix));
    } else if (n == 2) {
        /* double underscore: add class name prefix */

        const char *class_name = processor->container->GetQuotedClassName();
        if (class_name == nullptr)
            return;

        css_processor_replace_add(processor, name->start, name->start + 1,
                                  istream_string_new(processor->pool,
                                                     class_name));
    }
}

static void
css_processor_parser_block(void *ctx)
{
    CssProcessor *processor = (CssProcessor *)ctx;

    processor->uri_rewrite.mode = URI_MODE_PARTIAL;
    processor->uri_rewrite.view[0] = 0;
}

static void
css_processor_parser_property_keyword(const char *name, const char *value,
                                      off_t start, off_t end, void *ctx)
{
    CssProcessor *processor = (CssProcessor *)ctx;

    if (css_processor_option_rewrite_url(processor) &&
        strcmp(name, "-c-mode") == 0) {
        processor->uri_rewrite.mode = parse_uri_mode(value);
        css_processor_replace_add(processor, start, end, nullptr);
    }

    if (css_processor_option_rewrite_url(processor) &&
        strcmp(name, "-c-view") == 0) {
        g_strlcpy(processor->uri_rewrite.view, value,
                  sizeof(processor->uri_rewrite.view));
        css_processor_replace_add(processor, start, end, nullptr);
    }
}

static void
css_processor_parser_url(const CssParserValue *url, void *ctx)
{
    CssProcessor *processor = (CssProcessor *)ctx;

    if (!css_processor_option_rewrite_url(processor))
        return;

    Istream *istream =
        rewrite_widget_uri(*processor->pool,
                           *processor->env,
                           *global_translate_cache,
                           *processor->container,
                           url->value,
                           processor->uri_rewrite.mode, false,
                           processor->uri_rewrite.view[0] != 0
                           ? processor->uri_rewrite.view : nullptr,
                           &css_escape_class);
    if (istream != nullptr)
        css_processor_replace_add(processor, url->start, url->end, istream);
}

static void
css_processor_parser_import(const CssParserValue *url, void *ctx)
{
    CssProcessor *processor = (CssProcessor *)ctx;

    if (!css_processor_option_rewrite_url(processor))
        return;

    Istream *istream =
        rewrite_widget_uri(*processor->pool,
                           *processor->env,
                           *global_translate_cache,
                           *processor->container,
                           url->value,
                           URI_MODE_PARTIAL, false, nullptr,
                           &css_escape_class);
    if (istream != nullptr)
        css_processor_replace_add(processor, url->start, url->end, istream);
}

static void
css_processor_parser_eof(void *ctx, off_t length gcc_unused)
{
    CssProcessor *processor = (CssProcessor *)ctx;

    assert(processor->parser != nullptr);

    processor->parser = nullptr;

    istream_replace_finish(*processor->replace);
}

static void
css_processor_parser_error(GError *error, void *ctx)
{
    CssProcessor *processor = (CssProcessor *)ctx;

    assert(processor->parser != nullptr);

    processor->parser = nullptr;

    g_error_free(error);
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
CssProcessor::CssProcessor(struct pool &_pool, struct pool &_caller_pool,
                           Istream &tee,
                           Widget &_container,
                           struct processor_env &_env,
                           unsigned _options)
    :pool(&_pool), caller_pool(&_caller_pool),
     container(&_container), env(&_env),
     options(_options),
     replace(istream_replace_new(*pool, istream_tee_second(tee))),
     parser(css_parser_new(*pool, tee, false,
                           css_processor_parser_handler, this)) {}

Istream *
css_processor(struct pool &caller_pool, Istream &input,
              Widget &widget,
              struct processor_env &env,
              unsigned options)
{
    struct pool *pool = pool_new_linear(&caller_pool, "css_processor", 32768);

    Istream *tee = istream_tee_new(*pool, input,
                                   *env.event_loop,
                                   true, true);

    auto processor = NewFromPool<CssProcessor>(*pool, *pool, caller_pool, *tee,
                                               widget, env,
                                               options);
    pool_unref(pool);

    return processor->replace;
}
