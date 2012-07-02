/*
 * Rewrite URLs in CSS.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "css_rewrite.h"
#include "css_parser.h"
#include "rewrite-uri.h"
#include "pool.h"
#include "tpool.h"
#include "istream.h"
#include "istream-replace.h"
#include "strutil.h"

struct css_url {
    size_t start, end;
};

struct css_rewrite {
    struct css_parser *parser;

    unsigned n_urls;
    struct css_url urls[16];
};

/*
 * css_parser_handler
 *
 */

static void
css_rewrite_parser_url(const struct css_parser_value *url, void *ctx)
{
    struct css_rewrite *rewrite = ctx;
    assert(rewrite->parser != NULL);

    if (rewrite->n_urls < G_N_ELEMENTS(rewrite->urls)) {
        struct css_url *p = &rewrite->urls[rewrite->n_urls++];
        p->start = url->start;
        p->end = url->end;
    }
}

static void
css_rewrite_parser_eof(void *ctx, off_t length gcc_unused)
{
    struct css_rewrite *rewrite = ctx;
    assert(rewrite->parser != NULL);

    rewrite->parser = NULL;
}

static void
css_rewrite_parser_error(GError *error, void *ctx)
{
    struct css_rewrite *rewrite = ctx;
    (void)rewrite;

    g_error_free(error);
    assert(false);
}

static const struct css_parser_handler css_rewrite_parser_handler = {
    .url = css_rewrite_parser_url,
    .eof = css_rewrite_parser_eof,
    .error = css_rewrite_parser_error,
};

/*
 * constructor
 *
 */

struct istream *
css_rewrite_block_uris(struct pool *pool, struct pool *widget_pool,
                       struct tcache *translate_cache,
                       const char *absolute_uri,
                       const struct parsed_uri *external_uri,
                       const char *site_name, const char *untrusted_host,
                       struct strmap *args, struct widget *widget,
                       session_id_t session_id,
                       const struct strref block,
                       const struct escape_class *escape)
{
    struct pool_mark mark;
    pool_mark(tpool, &mark);

    struct css_rewrite rewrite = {
        .n_urls = 0,
    };

    rewrite.parser = css_parser_new(tpool,
                                    istream_memory_new(tpool, block.data,
                                                       block.length),
                                    true,
                                    &css_rewrite_parser_handler, &rewrite);
    css_parser_read(rewrite.parser);
    assert(rewrite.parser == NULL);

    pool_rewind(tpool, &mark);

    if (rewrite.n_urls == 0)
        /* no URLs found, no rewriting necessary */
        return NULL;

    struct istream *input =
        istream_memory_new(pool, p_memdup(pool, block.data, block.length),
                           block.length);
    struct istream *replace = istream_replace_new(pool, input);

    bool modified = false;
    for (unsigned i = 0; i < rewrite.n_urls; ++i) {
        const struct css_url *url = &rewrite.urls[i];

        struct strref buffer;
        strref_set(&buffer, block.data + url->start, url->end - url->start);

        struct istream *value =
            rewrite_widget_uri(pool, widget_pool, translate_cache,
                               absolute_uri, external_uri,
                               site_name, untrusted_host,
                               args,
                               widget,
                               session_id,
                               &buffer,
                               URI_MODE_PARTIAL, false, NULL,
                               escape);
        if (value == NULL)
            continue;

        istream_replace_add(replace, url->start, url->end, value);
        modified = true;
    }

    if (!modified) {
        istream_close(replace);
        return NULL;
    }

    istream_replace_finish(replace);
    return replace;
}
