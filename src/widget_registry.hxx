/*
 * Interface for the widget registry managed by the translation
 * server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_REGISTRY_HXX
#define BENG_PROXY_WIDGET_REGISTRY_HXX

struct pool;
struct tcache;
struct widget_class;
struct async_operation_ref;

typedef void (*widget_class_callback_t)(const struct widget_class *cls,
                                        void *ctx);

void
widget_class_lookup(struct pool *pool, struct pool *widget_pool,
                    struct tcache *translate_cache,
                    const char *widget_type,
                    widget_class_callback_t callback,
                    void *ctx,
                    struct async_operation_ref *async_ref);

#endif
