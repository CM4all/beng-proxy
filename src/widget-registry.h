/*
 * Interface for the widget registry managed by the translation
 * server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_WIDGET_REGISTRY_H
#define __BENG_WIDGET_REGISTRY_H

#include "pool.h"

struct tcache;
struct widget_class;
struct async_operation_ref;

typedef void (*widget_class_callback_t)(const struct widget_class *class,
                                        void *ctx);

void
widget_class_lookup(pool_t pool,
                    struct tcache *translate_cache,
                    const char *widget_type,
                    widget_class_callback_t callback,
                    void *ctx,
                    struct async_operation_ref *async_ref);

#endif
