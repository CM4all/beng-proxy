/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_PROCESSOR_ENV_H
#define BENG_PROXY_PROCESSOR_ENV_H

#include "session_id.h"

#include <http/method.h>

struct processor_env {
    struct pool *pool;

    const char *site_name;

    /**
     * If non-NULL, then only untrusted widgets with this host are
     * allowed; all trusted widgets are rejected.
     */
    const char *untrusted_host;

    const char *local_host;
    const char *remote_host;

    const char *uri;

    const char *absolute_uri;

    /** the URI which was requested by the beng-proxy client */
    const struct parsed_uri *external_uri;

    /** semicolon-arguments in the external URI */
    struct strmap *args;

    /**
     * The new path_info for the focused widget.
     */
    const char *path_info;

    /**
     * The view name of the top widget.
     */
    const char *view_name;

    /**
     * The HTTP method of the original request.
     */
    http_method_t method;

    struct strmap *request_headers;

    session_id_t session_id;
};

void
processor_env_init(struct pool *pool,
                   struct processor_env *env,
                   const char *site_name,
                   const char *untrusted_host,
                   const char *local_host,
                   const char *remote_host,
                   const char *request_uri,
                   const char *absolute_uri,
                   const struct parsed_uri *uri,
                   struct strmap *args,
                   session_id_t session_id,
                   http_method_t method,
                   struct strmap *request_headers);

#endif
