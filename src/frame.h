/*
 * Pick the output of a single widget for displaying it in an IFRAME.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FRAME_H
#define BENG_PROXY_FRAME_H

struct pool;
struct widget;
struct processor_env;
struct http_response_handler;
struct widget_lookup_handler;
struct async_operation_ref;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Request the contents of the specified widget.  This is a wrapper
 * for widget_http_request() with some additional checks (untrusted
 * host, session management).
 */
void
frame_top_widget(struct pool *pool, struct widget *widget,
                 struct processor_env *env,
                 const struct http_response_handler *handler,
                 void *handler_ctx,
                 struct async_operation_ref *async_ref);

/**
 * Looks up a child widget in the specified widget.  This is a wrapper
 * for widget_http_lookup() with some additional checks (untrusted
 * host, session management).
 */
void
frame_parent_widget(struct pool *pool, struct widget *widget, const char *id,
                    struct processor_env *env,
                    const struct widget_lookup_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref);

#ifdef __cplusplus
}
#endif

#endif
