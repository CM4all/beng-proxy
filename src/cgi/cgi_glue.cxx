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

void
cgi_new(SpawnService &spawn_service, EventLoop &event_loop,
        struct pool *pool, http_method_t method,
        const CgiAddress *address,
        const char *remote_addr,
        const StringMap &headers, Istream *body,
        HttpResponseHandler &handler,
        CancellablePointer &cancel_ptr)
{
    auto *stopwatch = stopwatch_new(pool, address->path);

    AbortFlag abort_flag(cancel_ptr);

    Istream *input;

    try {
        input = cgi_launch(event_loop, pool, method, address,
                                remote_addr, headers, body,
                                spawn_service);
    } catch (...) {
        if (abort_flag.aborted) {
            /* the operation was aborted - don't call the
               http_response_handler */
            return;
        }

        handler.InvokeError(std::current_exception());
        return;
    }

    stopwatch_event(stopwatch, "fork");

    cgi_client_new(*pool, stopwatch, *input, handler, cancel_ptr);
}
