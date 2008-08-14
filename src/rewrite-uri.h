/*
 * Rewrite URIs in templates.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_REWRITE_URI_H
#define __BENG_REWRITE_URI_H

#include "istream.h"
#include "session.h"

struct tcache;
struct parsed_uri;
struct strmap;
struct widget;
struct session;
struct strref;

enum uri_mode {
    URI_MODE_DIRECT,
    URI_MODE_FOCUS,
    URI_MODE_PARTIAL,
    URI_MODE_PARTITION,
    URI_MODE_PROXY,
};

istream_t
rewrite_widget_uri(pool_t pool, pool_t widget_pool,
                   struct tcache *translate_cache,
                   const char *partition_domain,
                   const struct parsed_uri *external_uri,
                   struct strmap *args, struct widget *widget,
                   session_id_t session_id,
                   const struct strref *value,
                   enum uri_mode mode);

#endif
