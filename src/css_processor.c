/*
 * Process URLs in a CSS stream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "css_processor.h"
#include "css_parser.h"
#include "penv.h"
#include "strmap.h"
#include "widget.h"
#include "widget-lookup.h"
#include "widget-class.h"
#include "growing-buffer.h"
#include "tpool.h"
#include "inline-widget.h"
#include "async.h"
#include "rewrite-uri.h"
#include "strref2.h"
#include "strref-pool.h"
#include "global.h"
#include "expansible-buffer.h"
#include "escape_css.h"

#include <daemon/log.h>

#include <glib.h>

#include <assert.h>
#include <string.h>

struct uri_rewrite {
    enum uri_mode mode;
};

struct css_processor {
    struct pool *pool, *caller_pool;

    struct widget *container;
    struct processor_env *env;

    istream_t replace;

    struct css_parser *parser;
    bool had_input;

    struct uri_rewrite uri_rewrite;

    /**
     * These values are used to buffer c:mode/c:base values in any
     * order, even after the actual URI attribute.
     */
    struct {
        bool pending;

        off_t uri_start, uri_end;
        struct expansible_buffer *value;

        /**
         * The positions of the c:mode/c:base attributes after the URI
         * attribute.  These have to be deleted *after* the URI
         * attribute has been rewritten.
         */
        struct {
            off_t start, end;
        } delete[4];
    } postponed_rewrite;

    struct async_operation async;
};

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
css_processor_parser_url(const struct css_parser_url *url, void *ctx)
{
    struct css_processor *processor = ctx;

    struct istream *istream =
        rewrite_widget_uri(processor->pool, processor->env->pool,
                           global_translate_cache,
                           processor->env->absolute_uri,
                           processor->env->external_uri,
                           processor->env->site_name,
                           processor->env->untrusted_host,
                           processor->env->args,
                           processor->container,
                           processor->env->session_id,
                           &url->value, URI_MODE_PROXY, false,
                           &css_escape_class);
    if (istream != NULL)
        css_processor_replace_add(processor, url->start, url->end, istream);
}

static void
css_processor_parser_eof(void *ctx, off_t length __attr_unused)
{
    struct css_processor *processor = ctx;

    assert(processor->parser != NULL);

    processor->parser = NULL;

    istream_replace_finish(processor->replace);
}

static void
css_processor_parser_error(GError *error, void *ctx)
{
    struct css_processor *processor = ctx;

    assert(processor->parser != NULL);

    processor->parser = NULL;

    g_error_free(error);
}

static const struct css_parser_handler css_processor_parser_handler = {
    .url = css_processor_parser_url,
    .eof = css_processor_parser_eof,
    .error = css_processor_parser_error,
};

/*
 * constructor
 *
 */

static void
headers_copy2(struct strmap *in, struct strmap *out,
              const char *const* keys)
{
    const char *value;

    for (; *keys != NULL; ++keys) {
        value = strmap_get(in, *keys);
        if (value != NULL)
            strmap_set(out, *keys, value);
    }
}

struct strmap *
css_processor_header_forward(pool_t pool, struct strmap *headers)
{
    if (headers == NULL)
        return NULL;

    static const char *const copy_headers[] = {
        "content-language",
        "content-type",
        "content-disposition",
        "location",
        NULL,
    };

    struct strmap *headers2 = strmap_new(pool, 8);
    headers_copy2(headers, headers2, copy_headers);
    return headers2;
}

struct istream *
css_processor(struct pool *caller_pool, struct istream *istream,
              struct widget *widget,
              struct processor_env *env)
{
    assert(istream != NULL);
    assert(!istream_has_handler(istream));

    struct pool *pool = pool_new_linear(caller_pool, "css_processor", 32768);
    struct css_processor *processor = p_malloc(pool, sizeof(*processor));
    processor->pool = pool;
    processor->caller_pool = caller_pool;

    processor->container = widget;
    processor->env = env;

    processor->postponed_rewrite.pending = false;
    processor->postponed_rewrite.value = expansible_buffer_new(pool, 1024);

    istream = istream_tee_new(processor->pool, istream, true, true);
    processor->replace = istream_replace_new(processor->pool,
                                             istream_tee_second(istream));

    processor->parser = css_parser_new(processor->pool, istream,
                                       &css_processor_parser_handler,
                                       processor);
    pool_unref(processor->pool);

    return processor->replace;
}
