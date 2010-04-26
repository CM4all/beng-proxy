/*
 * Interface for the widget registry managed by the translation
 * server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "widget-registry.h"
#include "processor.h"
#include "widget.h"
#include "tcache.h"
#include "uri-address.h"
#include "transformation.h"

static void
widget_registry_lookup(pool_t pool,
                       struct tcache *tcache,
                       const char *widget_type,
                       translate_callback_t callback,
                       void *ctx,
                       struct async_operation_ref *async_ref)
{
    struct translate_request *request = p_malloc(pool, sizeof(*request)); 

    request->local_address = NULL;
    request->local_address_length = 0;
    request->remote_host = NULL;
    request->host = NULL;
    request->user_agent = NULL;
    request->accept_language = NULL;
    request->authorization = NULL;
    request->uri = NULL;
    request->args = NULL;
    request->query_string = NULL;
    request->widget_type = widget_type;
    request->session = NULL;
    request->param = NULL;

    translate_cache(pool, tcache, request,
                    callback, ctx, async_ref);
}

struct widget_class_lookup {
    pool_t pool;

    widget_class_callback_t callback;
    void *callback_ctx;
};

static void 
lookup_callback(const struct translate_response *response, void *ctx)
{
    struct widget_class_lookup *lookup = ctx;
    struct widget_class *class;

    if (response->status != 0) {
        lookup->callback(NULL, lookup->callback_ctx);
        return;
    }

    class = p_malloc(lookup->pool, sizeof(*class));
    class->untrusted_host = response->untrusted;
    if (class->untrusted_host == NULL)
        /* compatibility with v0.7.16 */
        class->untrusted_host = response->host;
    class->stateful = response->stateful;
    resource_address_copy(lookup->pool, &class->address, &response->address);
    class->views = response->views != NULL
        ? transformation_dup_view_chain(lookup->pool, response->views)
        : NULL;

    class->request_header_forward = response->request_header_forward;
    class->response_header_forward = response->response_header_forward;

    lookup->callback(class, lookup->callback_ctx);
}


void
widget_class_lookup(pool_t pool, pool_t widget_pool,
                    struct tcache *tcache,
                    const char *widget_type,
                    widget_class_callback_t callback,
                    void *ctx,
                    struct async_operation_ref *async_ref)
{
    struct widget_class_lookup *lookup = p_malloc(pool, sizeof(*lookup));

    assert(widget_type != NULL);

    lookup->pool = widget_pool;
    lookup->callback = callback;
    lookup->callback_ctx = ctx;

    widget_registry_lookup(pool, tcache, widget_type,
                           lookup_callback, lookup,
                           async_ref);
}
