/*
 * Rewrite URLs in CSS.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CSS_REWRITE_HXX
#define BENG_PROXY_CSS_REWRITE_HXX

#include <stddef.h>

struct pool;
class Istream;
struct processor_env;
struct StringView;
struct parsed_uri;
struct istream;
struct escape_class;
struct widget;
struct tcache;

/**
 * @return NULL if no rewrite is necessary
 */
Istream *
css_rewrite_block_uris(struct pool &pool, struct pool &widget_pool,
                       struct processor_env &env,
                       struct tcache &translate_cache,
                       struct widget &widget,
                       StringView block,
                       const struct escape_class *escape);

#endif
