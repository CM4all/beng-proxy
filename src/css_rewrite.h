/*
 * Rewrite URLs in CSS.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CSS_REWRITE_H
#define BENG_PROXY_CSS_REWRITE_H

#include "session_id.h"

#include <stddef.h>

struct pool;
struct strref;
struct strmap;
struct parsed_uri;
struct istream;
struct escape_class;
struct widget;
struct tcache;

/**
 * @return NULL if no rewrite is necessary
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
                       const struct escape_class *escape);

#endif
