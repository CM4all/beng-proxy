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
    CssParser *parser;

    unsigned n_urls = 0;
    struct css_url urls[16];
};

/*
 * css_parser_handler
 *
 */

static void
css_rewrite_parser_url(const CssParserValue *url, void *ctx)
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

static constexpr CssParserHandler css_rewrite_parser_handler = {
    .class_name = nullptr,
    .xml_id = nullptr,
    .block = nullptr,
    .property_keyword = nullptr,
    .url = css_rewrite_parser_url,
    .import = nullptr,
    .eof = css_rewrite_parser_eof,
    .error = css_rewrite_parser_error,
};

/*
 * constructor
 *
 */

Istream *
css_rewrite_block_uris(struct pool &pool,
                       struct processor_env &env,
                       struct tcache &translate_cache,
                       Widget &widget,
                       const StringView block,
                       const struct escape_class *escape)
{
    struct css_rewrite rewrite;

    {
        const AutoRewindPool auto_rewind(*tpool);

        rewrite.parser = css_parser_new(*tpool,
                                        *istream_memory_new(tpool, block.data,
                                                            block.size),
                                        true,
                                        css_rewrite_parser_handler, &rewrite);
        css_parser_read(rewrite.parser);
    }

    assert(rewrite.parser == nullptr);

    if (rewrite.n_urls == 0)
        /* no URLs found, no rewriting necessary */
        return nullptr;

    Istream *input =
        istream_memory_new(&pool, p_strdup(pool, block), block.size);
    Istream *replace = istream_replace_new(pool, *input);

    bool modified = false;
    for (unsigned i = 0; i < rewrite.n_urls; ++i) {
        const struct css_url *url = &rewrite.urls[i];

        Istream *value =
            rewrite_widget_uri(pool, env, translate_cache,
                               widget,
                               {block.data + url->start, url->end - url->start},
                               URI_MODE_PARTIAL, false, nullptr,
                               escape);
        if (value == nullptr)
            continue;

        istream_replace_add(*replace, url->start, url->end, value);
        modified = true;
    }

    if (!modified) {
        replace->CloseUnused();
        return nullptr;
    }

    istream_replace_finish(*replace);
    return replace;
}
