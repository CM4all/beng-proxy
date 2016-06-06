/*
 * Send HTTP requests to a widget server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_HTTP_HXX
#define BENG_PROXY_WIDGET_HTTP_HXX

struct pool;
struct Widget;
struct widget_lookup_handler;
struct processor_env;
struct http_response_handler;
struct async_operation_ref;

/**
 * Sends a HTTP request to the widget, apply all transformations, and
 * return the result to the #http_response_handler.
 */
void
widget_http_request(struct pool &pool, Widget &widget,
                    struct processor_env &env,
                    const struct http_response_handler &handler,
                    void *handler_ctx,
                    struct async_operation_ref &async_ref);

/**
 * Send a HTTP request to the widget server, process it, and look up
 * the specified widget in the processed result.
 *
 * @param widget the widget that represents the template
 * @param id the id of the widget to be looked up
 */
void
widget_http_lookup(struct pool &pool, Widget &widget, const char *id,
                   struct processor_env &env,
                   const struct widget_lookup_handler &handler,
                   void *handler_ctx,
                   struct async_operation_ref &async_ref);

#endif
