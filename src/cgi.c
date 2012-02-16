/*
 * Run a CGI script.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cgi.h"
#include "cgi-address.h"
#include "cgi-client.h"
#include "cgi-launch.h"
#include "abort-flag.h"
#include "stopwatch.h"
#include "http-response.h"
#include "istream.h"

void
cgi_new(struct pool *pool, http_method_t method,
        const struct cgi_address *address,
        const char *remote_addr,
        struct strmap *headers, struct istream *body,
        const struct http_response_handler *handler,
        void *handler_ctx,
        struct async_operation_ref *async_ref)
{
    struct stopwatch *stopwatch = stopwatch_new(pool, address->path);

    struct abort_flag abort_flag;
    abort_flag_set(&abort_flag, async_ref);

    GError *error = NULL;
    struct istream *input = cgi_launch(pool, method, address,
                                       remote_addr, headers, body, &error);
    if (input == NULL) {
        if (abort_flag.aborted) {
            /* the operation was aborted - don't call the
               http_response_handler */
            g_error_free(error);
            return;
        }

        http_response_handler_direct_abort(handler, handler_ctx, error);
        return;
    }

    stopwatch_event(stopwatch, "fork");

    cgi_client_new(pool, stopwatch, input, handler, handler_ctx, async_ref);
}
