/*
 * Wrapper for widget-registry.h which resolves widget classes.  This
 * library can manage several concurrent requests for one widget
 * object.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_RESOLVER_HXX
#define BENG_PROXY_WIDGET_RESOLVER_HXX

struct pool;
struct widget;
struct tcache;
struct widget_class;
struct async_operation_ref;

typedef void (*widget_resolver_callback_t)(void *ctx);

void
widget_resolver_new(struct pool *pool, struct pool *widget_pool,
                    struct widget *widget,
                    struct tcache *translate_cache,
                    widget_resolver_callback_t callback, void *ctx,
                    struct async_operation_ref *async_ref);

#endif
