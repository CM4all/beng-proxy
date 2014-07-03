/*
 * Interface for the widget registry managed by the translation
 * server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget_registry.hxx"
#include "widget_class.hxx"
#include "processor.h"
#include "widget.h"
#include "tcache.hxx"
#include "translate_client.hxx"
#include "translate_request.hxx"
#include "translate_response.hxx"
#include "transformation.hxx"
#include "pool.h"

#include <daemon/log.h>

#include <glib.h>

static void
widget_registry_lookup(struct pool *pool,
                       struct tcache *tcache,
                       const char *widget_type,
                       const TranslateHandler *handler, void *ctx,
                       struct async_operation_ref *async_ref)
{
    auto request = NewFromPool<TranslateRequest>(pool);
    request->Clear();

    request->widget_type = widget_type;

    translate_cache(pool, tcache, request,
                    handler, ctx, async_ref);
}

struct widget_class_lookup {
    struct pool *pool;

    widget_class_callback_t callback;
    void *callback_ctx;
};

static void
widget_translate_response(TranslateResponse *response, void *ctx)
{
    struct widget_class_lookup *lookup = (struct widget_class_lookup *)ctx;

    if (response->status != 0) {
        lookup->callback(nullptr, lookup->callback_ctx);
        return;
    }

    widget_class *cls = NewFromPool<widget_class>(lookup->pool);
    cls->local_uri = response->local_uri;
    cls->untrusted_host = response->untrusted;
    cls->untrusted_prefix = response->untrusted_prefix;
    cls->untrusted_site_suffix = response->untrusted_site_suffix;
    if (cls->untrusted_host == nullptr)
        /* compatibility with v0.7.16 */
        cls->untrusted_host = response->host;
    cls->cookie_host = response->cookie_host;
    cls->group = response->widget_group;
    cls->container_groups = response->container_groups;
    cls->direct_addressing = response->direct_addressing;
    cls->stateful = response->stateful;
    cls->anchor_absolute = response->anchor_absolute;
    cls->info_headers = response->widget_info;
    cls->dump_headers = response->dump_headers;
    if (response->views != nullptr)
        cls->views = *widget_view_dup_chain(lookup->pool, response->views);
    else
        cls->views.Init();

    lookup->callback(cls, lookup->callback_ctx);
}

static void
widget_translate_error(GError *error, void *ctx)
{
    struct widget_class_lookup *lookup = (struct widget_class_lookup *)ctx;

    daemon_log(2, "widget registry error: %s\n", error->message);
    g_error_free(error);

    lookup->callback(nullptr, lookup->callback_ctx);
}

static const TranslateHandler widget_translate_handler = {
    .response = widget_translate_response,
    .error = widget_translate_error,
};

void
widget_class_lookup(struct pool *pool, struct pool *widget_pool,
                    struct tcache *tcache,
                    const char *widget_type,
                    widget_class_callback_t callback,
                    void *ctx,
                    struct async_operation_ref *async_ref)
{
    struct widget_class_lookup *lookup =
        NewFromPool<struct widget_class_lookup>(pool);

    assert(widget_type != nullptr);

    lookup->pool = widget_pool;
    lookup->callback = callback;
    lookup->callback_ctx = ctx;

    widget_registry_lookup(pool, tcache, widget_type,
                           &widget_translate_handler, lookup,
                           async_ref);
}
