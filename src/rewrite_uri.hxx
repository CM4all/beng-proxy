/*
 * Rewrite URIs in templates.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_REWRITE_URI_H
#define __BENG_REWRITE_URI_H

#include "session_id.h"

#include <glib.h>

struct pool;
struct tcache;
struct parsed_uri;
struct strmap;
struct widget;
struct strref;
struct escape_class;

enum uri_mode {
    URI_MODE_DIRECT,
    URI_MODE_FOCUS,
    URI_MODE_PARTIAL,
};

#ifdef __cplusplus
extern "C" {
#endif

G_GNUC_PURE
enum uri_mode
parse_uri_mode(const struct strref *s);

/**
 * @param untrusted_host the value of the UNTRUSTED translation
 * packet, or NULL if this is a "trusted" request
 * @param stateful if true, then the current request/session state is
 * taken into account (path_info and query_string)
 * @param view the name of a view, or NULL to use the default view
 */
G_GNUC_MALLOC
struct istream *
rewrite_widget_uri(struct pool *pool, struct pool *widget_pool,
                   struct tcache *translate_cache,
                   const char *absolute_uri,
                   const struct parsed_uri *external_uri,
                   const char *site_name,
                   const char *untrusted_host,
                   struct strmap *args, struct widget *widget,
                   session_id_t session_id,
                   const struct strref *value,
                   enum uri_mode mode, bool stateful,
                   const char *view,
                   const struct escape_class *escape);

#ifdef __cplusplus
}
#endif

#endif
