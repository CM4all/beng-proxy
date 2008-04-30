/*
 * Wrapper for widget-registry.h which resolves widget classes.  This
 * library can manage several concurrent requests for one widget
 * object.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_WIDGET_RESOLVER_H
#define __BENG_WIDGET_RESOLVER_H

#include "pool.h"

struct widget;
struct tcache;
struct widget_class;
struct async_operation_ref;

typedef void (*widget_resolver_callback_t)(void *ctx);

void
widget_resolver_new(pool_t pool, pool_t widget_pool, struct widget *widget,
                    struct tcache *translate_cache,
                    widget_resolver_callback_t callback, void *ctx,
                    struct async_operation_ref *async_ref);

#endif
