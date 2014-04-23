/*
 * Rewrite URIs in templates.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_REWRITE_URI_H
#define __BENG_REWRITE_URI_H

#include <inline/compiler.h>

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

    /**
     * Embed the widget's HTTP response instead of generating an URI
     * to the widget server.
     */
    URI_MODE_RESPONSE,
};

gcc_pure
enum uri_mode
parse_uri_mode(const struct strref *s);

/**
 * @param untrusted_host the value of the UNTRUSTED translation
 * packet, or NULL if this is a "trusted" request
 * @param stateful if true, then the current request/session state is
 * taken into account (path_info and query_string)
 * @param view the name of a view, or NULL to use the default view
 */
gcc_malloc
struct istream *
rewrite_widget_uri(struct pool *pool, struct pool *widget_pool,
                   struct processor_env *env,
                   struct tcache *translate_cache,
                   struct widget *widget,
                   const struct strref *value,
                   enum uri_mode mode, bool stateful,
                   const char *view,
                   const struct escape_class *escape);

#endif
