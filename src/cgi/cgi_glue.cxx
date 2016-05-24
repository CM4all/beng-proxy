/*
 * Run a CGI script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cgi_glue.hxx"
#include "cgi_address.hxx"
#include "cgi_client.hxx"
#include "cgi_launch.hxx"
#include "abort_flag.hxx"
#include "stopwatch.hxx"
#include "http_response.hxx"
#include "istream/istream.hxx"

#include <glib.h>

void
cgi_new(SpawnService &spawn_service,
        struct pool *pool, http_method_t method,
        const CgiAddress *address,
        const char *remote_addr,
        struct strmap *headers, Istream *body,
        const struct http_response_handler *handler,
        void *handler_ctx,
        struct async_operation_ref *async_ref)
{
    auto *stopwatch = stopwatch_new(pool, address->path);

    AbortFlag abort_flag(*async_ref);

    GError *error = nullptr;
    Istream *input = cgi_launch(pool, method, address,
                                remote_addr, headers, body,
                                spawn_service, &error);
    if (input == nullptr) {
        if (abort_flag.aborted) {
            /* the operation was aborted - don't call the
               http_response_handler */
            g_error_free(error);
            return;
        }

        handler->InvokeAbort(handler_ctx, error);
        return;
    }

    stopwatch_event(stopwatch, "fork");

    cgi_client_new(*pool, stopwatch, *input, *handler, handler_ctx, *async_ref);
}
