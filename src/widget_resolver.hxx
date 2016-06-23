/*
 * Wrapper for widget-registry.h which resolves widget classes.  This
 * library can manage several concurrent requests for one widget
 * object.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_RESOLVER_HXX
#define BENG_PROXY_WIDGET_RESOLVER_HXX

#include "util/BindMethod.hxx"

struct pool;
struct Widget;
struct tcache;
struct async_operation_ref;

typedef BoundMethod<void()> WidgetResolverCallback;

void
ResolveWidget(struct pool &pool,
              Widget &widget,
              struct tcache &translate_cache,
              WidgetResolverCallback callback,
              struct async_operation_ref &async_ref);

#endif
