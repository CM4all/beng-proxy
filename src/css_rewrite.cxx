/*
 * Rewrite URLs in CSS.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "css_rewrite.hxx"
#include "css_parser.hxx"
#include "rewrite_uri.hxx"
#include "pool.hxx"
#include "tpool.hxx"
#include "istream/istream.hxx"
#include "istream/istream_memory.hxx"
#include "istream/istream_replace.hxx"
#include "util/Macros.hxx"
#include "util/StringView.hxx"

#include <glib.h>

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
    struct css_rewrite *rewrite = (struct css_rewrite *)ctx;
    assert(rewrite->parser != nullptr);

    if (rewrite->n_urls < ARRAY_SIZE(rewrite->urls)) {
        struct css_url *p = &rewrite->urls[rewrite->n_urls++];
        p->start = url->start;
        p->end = url->end;
    }
}

static void
css_rewrite_parser_eof(void *ctx, off_t length gcc_unused)
{
    struct css_rewrite *rewrite = (struct css_rewrite *)ctx;
    assert(rewrite->parser != nullptr);

    rewrite->parser = nullptr;
}

#ifndef NDEBUG
gcc_noreturn
#endif
static void
css_rewrite_parser_error(GError *error, void *ctx)
{
    struct css_rewrite *rewrite = (struct css_rewrite *)ctx;
    (void)rewrite;

    /* shouldn't happen - input is an istream_memory which never
       fails */
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
css_rewrite_block_uris(struct pool &pool, struct pool &widget_pool,
                       struct processor_env &env,
                       struct tcache &translate_cache,
                       struct widget &widget,
                       const struct strref block,
                       const struct escape_class *escape)
{
    struct pool_mark_state mark;
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
    assert(rewrite.parser == nullptr);

    pool_rewind(tpool, &mark);

    if (rewrite.n_urls == 0)
        /* no URLs found, no rewriting necessary */
        return nullptr;

    struct istream *input =
        istream_memory_new(&pool, p_memdup(&pool, block.data, block.length),
                           block.length);
    struct istream *replace = istream_replace_new(&pool, input);

    bool modified = false;
    for (unsigned i = 0; i < rewrite.n_urls; ++i) {
        const struct css_url *url = &rewrite.urls[i];

        struct istream *value =
            rewrite_widget_uri(pool, widget_pool, env, translate_cache,
                               widget,
                               {block.data + url->start, url->end - url->start},
                               URI_MODE_PARTIAL, false, nullptr,
                               escape);
        if (value == nullptr)
            continue;

        istream_replace_add(replace, url->start, url->end, value);
        modified = true;
    }

    if (!modified) {
        istream_close(replace);
        return nullptr;
    }

    istream_replace_finish(replace);
    return replace;
}
