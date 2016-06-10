/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_PROCESSOR_ENV_H
#define BENG_PROXY_PROCESSOR_ENV_H

#include "session_id.hxx"

#include <http/method.h>

class EventLoop;
class ResourceLoader;
struct SessionLease;

struct processor_env {
    struct pool *pool;

    EventLoop *event_loop;

    ResourceLoader *resource_loader;
    ResourceLoader *filter_resource_loader;

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

    /**
     * The name of the session cookie.
     */
    const char *session_cookie;

    SessionId session_id;

    processor_env() = default;

    processor_env(struct pool *pool,
                  EventLoop &_event_loop,
                  ResourceLoader &_resource_loader,
                  ResourceLoader &_filter_resource_loader,
                  const char *site_name,
                  const char *untrusted_host,
                  const char *local_host,
                  const char *remote_host,
                  const char *request_uri,
                  const char *absolute_uri,
                  const struct parsed_uri *uri,
                  struct strmap *args,
                  const char *session_cookie,
                  SessionId session_id,
                  http_method_t method,
                  struct strmap *request_headers);

    SessionLease GetSession() const;
};

#endif
