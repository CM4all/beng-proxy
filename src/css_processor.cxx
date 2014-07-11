/*
 * Process URLs in a CSS stream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "css_processor.h"
#include "css_parser.hxx"
#include "css_util.hxx"
#include "penv.hxx"
#include "strmap.hxx"
#include "widget.h"
#include "widget-lookup.h"
#include "growing_buffer.hxx"
#include "tpool.h"
#include "async.h"
#include "rewrite_uri.hxx"
#include "strref2.h"
#include "strref-pool.h"
#include "global.h"
#include "expansible-buffer.h"
#include "escape_css.h"
#include "istream.h"
#include "istream_replace.hxx"
#include "istream_tee.h"
#include "pool.hxx"

#include <daemon/log.h>

#include <glib.h>

#include <assert.h>
#include <string.h>

struct uri_rewrite {
    enum uri_mode mode;

    char view[64];
};

struct css_processor {
    struct pool *pool, *caller_pool;

    struct widget *container;
    struct processor_env *env;
    unsigned options;

    struct istream *replace;

    struct css_parser *parser;
    bool had_input;

    struct uri_rewrite uri_rewrite;

    struct async_operation async;
};

static inline bool
css_processor_option_rewrite_url(const struct css_processor *processor)
{
    return (processor->options & CSS_PROCESSOR_REWRITE_URL) != 0;
}

static inline bool
css_processor_option_prefix_class(const struct css_processor *processor)
{
    return (processor->options & CSS_PROCESSOR_PREFIX_CLASS) != 0;
}

static inline bool
css_processor_option_prefix_id(const struct css_processor *processor)
{
    return (processor->options & CSS_PROCESSOR_PREFIX_ID) != 0;
}

static void
css_processor_replace_add(struct css_processor *processor,
                          off_t start, off_t end,
                          struct istream *istream)
{
    istream_replace_add(processor->replace, start, end, istream);
}

/*
 * css_parser_handler
 *
 */

static void
css_processor_parser_class_name(const struct css_parser_value *name, void *ctx)
{
    struct css_processor *processor = (struct css_processor *)ctx;

    assert(name->value.length > 0);

    if (!css_processor_option_prefix_class(processor))
        return;

    unsigned n = underscore_prefix(name->value.data, strref_end(&name->value));
    if (n == 3) {
        /* triple underscore: add widget path prefix */

        const char *prefix = widget_prefix(processor->container);
        if (prefix == nullptr)
            return;

        css_processor_replace_add(processor, name->start, name->start + 3,
                                  istream_string_new(processor->pool,
                                                     prefix));
    } else if (n == 2) {
        /* double underscore: add class name prefix */

        const char *class_name =
            widget_get_quoted_class_name(processor->container);
        if (class_name == nullptr)
            return;

        css_processor_replace_add(processor, name->start, name->start + 2,
                                  istream_string_new(processor->pool,
                                                     class_name));
    }
}

static void
css_processor_parser_xml_id(const struct css_parser_value *name, void *ctx)
{
    struct css_processor *processor = (struct css_processor *)ctx;

    assert(name->value.length > 0);

    if (!css_processor_option_prefix_id(processor))
        return;

    unsigned n = underscore_prefix(name->value.data, strref_end(&name->value));
    if (n == 3) {
        /* triple underscore: add widget path prefix */

        const char *prefix = widget_prefix(processor->container);
        if (prefix == nullptr)
            return;

        css_processor_replace_add(processor, name->start, name->start + 3,
                                  istream_string_new(processor->pool,
                                                     prefix));
    } else if (n == 2) {
        /* double underscore: add class name prefix */

        const char *class_name =
            widget_get_quoted_class_name(processor->container);
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
    struct css_processor *processor = (struct css_processor *)ctx;

    processor->uri_rewrite.mode = URI_MODE_PARTIAL;
    processor->uri_rewrite.view[0] = 0;
}

static void
css_processor_parser_property_keyword(const char *name, const char *value,
                                      off_t start, off_t end, void *ctx)
{
    struct css_processor *processor = (struct css_processor *)ctx;

    if (css_processor_option_rewrite_url(processor) &&
        strcmp(name, "-c-mode") == 0) {
        struct strref value2;
        strref_set_c(&value2, value);
        processor->uri_rewrite.mode = parse_uri_mode(&value2);
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
css_processor_parser_url(const struct css_parser_value *url, void *ctx)
{
    struct css_processor *processor = (struct css_processor *)ctx;

    if (!css_processor_option_rewrite_url(processor))
        return;

    struct istream *istream =
        rewrite_widget_uri(processor->pool, processor->env->pool,
                           processor->env,
                           global_translate_cache,
                           processor->container,
                           &url->value, processor->uri_rewrite.mode, false,
                           processor->uri_rewrite.view[0] != 0
                           ? processor->uri_rewrite.view : nullptr,
                           &css_escape_class);
    if (istream != nullptr)
        css_processor_replace_add(processor, url->start, url->end, istream);
}

static void
css_processor_parser_import(const struct css_parser_value *url, void *ctx)
{
    struct css_processor *processor = (struct css_processor *)ctx;

    if (!css_processor_option_rewrite_url(processor))
        return;

    struct istream *istream =
        rewrite_widget_uri(processor->pool, processor->env->pool,
                           processor->env,
                           global_translate_cache,
                           processor->container,
                           &url->value, URI_MODE_PARTIAL, false, nullptr,
                           &css_escape_class);
    if (istream != nullptr)
        css_processor_replace_add(processor, url->start, url->end, istream);
}

static void
css_processor_parser_eof(void *ctx, off_t length gcc_unused)
{
    struct css_processor *processor = (struct css_processor *)ctx;

    assert(processor->parser != nullptr);

    processor->parser = nullptr;

    istream_replace_finish(processor->replace);
}

static void
css_processor_parser_error(GError *error, void *ctx)
{
    struct css_processor *processor = (struct css_processor *)ctx;

    assert(processor->parser != nullptr);

    processor->parser = nullptr;

    g_error_free(error);
}

static const struct css_parser_handler css_processor_parser_handler = {
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

struct istream *
css_processor(struct pool *caller_pool, struct istream *istream,
              struct widget *widget,
              struct processor_env *env,
              unsigned options)
{
    assert(istream != nullptr);
    assert(!istream_has_handler(istream));

    struct pool *pool = pool_new_linear(caller_pool, "css_processor", 32768);
    auto processor = NewFromPool<struct css_processor>(pool);
    processor->pool = pool;
    processor->caller_pool = caller_pool;

    processor->container = widget;
    processor->env = env;
    processor->options = options;

    istream = istream_tee_new(processor->pool, istream, true, true);
    processor->replace = istream_replace_new(processor->pool,
                                             istream_tee_second(istream));

    processor->parser = css_parser_new(processor->pool, istream, false,
                                       &css_processor_parser_handler,
                                       processor);
    pool_unref(processor->pool);

    return processor->replace;
}
