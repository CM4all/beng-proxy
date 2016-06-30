/*
 * Pick the output of a single widget for displaying it in an IFRAME.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FRAME_HXX
#define BENG_PROXY_FRAME_HXX

struct pool;
struct Widget;
struct processor_env;
class HttpResponseHandler;
struct async_operation_ref;
class WidgetLookupHandler;

/**
 * Request the contents of the specified widget.  This is a wrapper
 * for widget_http_request() with some additional checks (untrusted
 * host, session management).
 */
void
frame_top_widget(struct pool *pool, Widget *widget,
                 struct processor_env *env,
                 HttpResponseHandler &_handler,
                 struct async_operation_ref *async_ref);

/**
 * Looks up a child widget in the specified widget.  This is a wrapper
 * for widget_http_lookup() with some additional checks (untrusted
 * host, session management).
 */
void
frame_parent_widget(struct pool *pool, Widget *widget, const char *id,
                    struct processor_env *env,
                    WidgetLookupHandler &handler,
                    struct async_operation_ref *async_ref);

#endif
