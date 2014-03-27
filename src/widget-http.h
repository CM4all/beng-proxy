/*
 * Send HTTP requests to a widget server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_WIDGET_HTTP_H
#define __BENG_WIDGET_HTTP_H

struct pool;
struct widget;
struct widget_lookup_handler;
struct processor_env;
struct http_response_handler;
struct async_operation_ref;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Sends a HTTP request to the widget, apply all transformations, and
 * return the result to the #http_response_handler.
 */
void
widget_http_request(struct pool *pool, struct widget *widget,
                    struct processor_env *env,
                    const struct http_response_handler *handler,
                    void *handler_ctx,
                    struct async_operation_ref *async_ref);

/**
 * Send a HTTP request to the widget server, process it, and look up
 * the specified widget in the processed result.
 *
 * @param widget the widget that represents the template
 * @param id the id of the widget to be looked up
 */
void
widget_http_lookup(struct pool *pool, struct widget *widget, const char *id,
                   struct processor_env *env,
                   const struct widget_lookup_handler *handler,
                   void *handler_ctx,
                   struct async_operation_ref *async_ref);

#ifdef __cplusplus
}
#endif

#endif
