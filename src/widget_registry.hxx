/*
 * Interface for the widget registry managed by the translation
 * server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_REGISTRY_HXX
#define BENG_PROXY_WIDGET_REGISTRY_HXX

#include "util/BindMethod.hxx"

struct pool;
struct tcache;
struct async_operation_ref;
struct WidgetClass;

typedef BoundMethod<void(const WidgetClass *cls)> WidgetRegistryCallback;

void
widget_class_lookup(struct pool &pool, struct pool &widget_pool,
                    struct tcache &translate_cache,
                    const char *widget_type,
                    WidgetRegistryCallback callback,
                    struct async_operation_ref &async_ref);

#endif
