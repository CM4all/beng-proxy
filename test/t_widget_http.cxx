#include "widget.hxx"
#include "widget_class.hxx"
#include "widget_http.hxx"
#include "widget_lookup.hxx"
#include "http_address.hxx"
#include "strmap.hxx"
#include "growing_buffer.hxx"
#include "header_parser.hxx"
#include "tpool.hxx"
#include "get.hxx"
#include "http_response.hxx"
#include "processor.h"
#include "css_processor.h"
#include "text_processor.hxx"
#include "penv.hxx"
#include "async.hxx"
#include "fcache.hxx"
#include "transformation.hxx"
#include "crash.hxx"
#include "istream.h"
#include "istream_null.hxx"
#include "session_manager.hxx"
#include "session.hxx"
#include "suffix_registry.hxx"
#include "address_suffix_registry.hxx"

#include <inline/compiler.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <event.h>

struct request {
    bool cached;
    http_method_t method;
    const char *uri;
    const char *request_headers;

    http_status_t status;
    const char *response_headers;
    const char *response_body;
};

int global_filter_cache;
int global_delegate_stock;
int global_lhttp_stock;
int global_fcgi_stock;
int global_was_stock;
int global_http_cache;
int global_nfs_cache;
int global_tcp_balancer;
const WidgetClass root_widget_class = {
    .container_groups = StringSet(),
};

static unsigned test_id;
static bool got_request, got_response;

bool
processable(gcc_unused const struct strmap *headers)
{
    return false;
}

struct istream *
processor_process(gcc_unused struct pool *pool, struct istream *istream,
                  gcc_unused struct widget *widget,
                  gcc_unused struct processor_env *env,
                  gcc_unused unsigned options)
{
    return istream;
}

void
processor_lookup_widget(gcc_unused struct pool *pool,
                        gcc_unused struct istream *istream,
                        gcc_unused struct widget *widget,
                        gcc_unused const char *id,
                        gcc_unused struct processor_env *env,
                        gcc_unused unsigned options,
                        const struct widget_lookup_handler *handler,
                        void *handler_ctx,
                        gcc_unused struct async_operation_ref *async_ref)
{
    handler->not_found(handler_ctx);
}

struct istream *
css_processor(gcc_unused struct pool *pool, struct istream *stream,
              gcc_unused struct widget *widget,
              gcc_unused struct processor_env *env,
              gcc_unused unsigned options)
{
    return stream;
}

bool
text_processor_allowed(gcc_unused const struct strmap *headers)
{
    return false;
}

struct istream *
text_processor(gcc_unused struct pool *pool, struct istream *stream,
               gcc_unused const struct widget *widget,
               gcc_unused const struct processor_env *env)
{
    return stream;
}

struct filter_cache;

void
filter_cache_request(gcc_unused struct filter_cache *cache,
                     gcc_unused struct pool *pool,
                     gcc_unused const struct resource_address *address,
                     gcc_unused const char *source_id,
                     gcc_unused http_status_t status,
                     gcc_unused struct strmap *headers,
                     gcc_unused struct istream *body,
                     const struct http_response_handler *handler,
                     void *handler_ctx,
                     gcc_unused struct async_operation_ref *async_ref)
{
    GError *error = g_error_new_literal(g_quark_from_static_string("test"), 0,
                                        "Test");
    handler->InvokeAbort(handler_ctx, error);
}

bool
suffix_registry_lookup(gcc_unused struct pool &pool,
                       gcc_unused struct tcache &translate_cache,
                       gcc_unused const struct resource_address &address,
                       gcc_unused const SuffixRegistryHandler &handler,
                       gcc_unused void *ctx,
                       gcc_unused struct async_operation_ref &async_ref)
{
    return false;
}

struct tcache *global_translate_cache;

struct Stock;
Stock *global_pipe_stock;

struct istream *
istream_pipe_new(gcc_unused struct pool *pool, struct istream *input,
                 gcc_unused Stock *pipe_stock)
{
    return input;
}

struct http_cache;
struct tcp_stock;
struct fcgi_stock;
struct hstock;

void
resource_get(gcc_unused struct http_cache *cache,
             gcc_unused struct tcp_balancer *tcp_balancer,
             gcc_unused struct lhttp_stock *lhttp_stock,
             gcc_unused struct fcgi_stock *fcgi_stock,
             gcc_unused StockMap *was_stock,
             gcc_unused StockMap *delegate_stock,
             gcc_unused struct nfs_cache *nfs_cache,
             struct pool *pool,
             gcc_unused unsigned session_sticky,
             http_method_t method,
             gcc_unused const struct resource_address *address,
             http_status_t status, struct strmap *headers, struct istream *body,
             const struct http_response_handler *handler,
             void *handler_ctx,
             gcc_unused struct async_operation_ref *async_ref)
{
    struct strmap *response_headers = strmap_new(pool);
    struct istream *response_body = istream_null_new(pool);
    const char *p;

    assert(!got_request);
    assert(method == HTTP_METHOD_GET);
    assert(body == nullptr);

    got_request = true;

    if (body != nullptr)
        istream_close_unused(body);

    switch (test_id) {
    case 0:
        p = headers->Get("cookie");
        assert(p == nullptr);

        /* set one cookie */
        response_headers->Add("set-cookie", "foo=bar");
        break;

    case 1:
        /* is the one cookie present? */
        p = headers->Get("cookie");
        assert(p != nullptr);
        assert(strcmp(p, "foo=bar") == 0);

        /* add 2 more cookies */
        response_headers->Add("set-cookie", "a=b, c=d");
        break;

    case 2:
        /* are 3 cookies present? */
        p = headers->Get("cookie");
        assert(p != nullptr);
        assert(strcmp(p, "c=d; a=b; foo=bar") == 0);

        /* set two cookies in two headers */
        response_headers->Add("set-cookie", "e=f");
        response_headers->Add("set-cookie", "g=h");
        break;

    case 3:
        /* check for 5 cookies */
        p = headers->Get("cookie");
        assert(p != nullptr);
        assert(strcmp(p, "g=h; e=f; c=d; a=b; foo=bar") == 0);
        break;
    }

    handler->InvokeResponse(handler_ctx, status, response_headers,
                            response_body);
}

static void
my_http_response(http_status_t status, gcc_unused struct strmap *headers,
                 struct istream *body, gcc_unused void *ctx)
{
    assert(!got_response);
    assert(status == 200);
    assert(body != nullptr);

    istream_close_unused(body);

    got_response = true;
}

static void gcc_noreturn
my_http_abort(GError *error, gcc_unused void *ctx)
{
    g_printerr("%s\n", error->message);
    g_error_free(error);

    assert(false);
}

static const struct http_response_handler my_http_response_handler = {
    .response = my_http_response,
    .abort = my_http_abort,
};

static void
test_cookie_client(struct pool *pool)
{
    static struct http_address address = {
        .scheme = URI_SCHEME_HTTP,
        .host_and_port = "foo",
        .path = "/bar/",
    };
    static const WidgetClass cls = {
        .views = {
            .address = {
                .type = RESOURCE_ADDRESS_HTTP,
                .u = {
                    .http = &address,
                },
            },

            .request_header_forward = {
                .modes = {
                    [HEADER_GROUP_IDENTITY] = HEADER_FORWARD_MANGLE,
                    [HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_YES,
                    [HEADER_GROUP_COOKIE] = HEADER_FORWARD_MANGLE,
                    [HEADER_GROUP_OTHER] = HEADER_FORWARD_NO,
                },
            },
            .response_header_forward = {
                .modes = {
                    [HEADER_GROUP_IDENTITY] = HEADER_FORWARD_NO,
                    [HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_YES,
                    [HEADER_GROUP_COOKIE] = HEADER_FORWARD_MANGLE,
                    [HEADER_GROUP_OTHER] = HEADER_FORWARD_NO,
                },
            }
        },

        .stateful = true,
        .dump_headers = false,
        .container_groups = StringSet(),
    };
    struct widget widget;
    struct async_operation_ref async_ref;

    auto *session = session_new("");

    struct processor_env env;
    env.local_host = "localhost";
    env.remote_host = "localhost";
    env.request_headers = strmap_new(pool);
    env.session_id = session->id;
    session_put(session);

    widget.Init(*pool, &cls);

    for (test_id = 0; test_id < 4; ++test_id) {
        got_request = false;
        got_response = false;

        widget_http_request(*pool, widget, env,
                            my_http_response_handler, nullptr,
                            async_ref);

        assert(got_request);
        assert(got_response);
    }
}

int main(int argc, char **argv) {
    struct event_base *event_base;
    bool success;
    struct pool *pool;

    (void)argc;
    (void)argv;

    event_base = event_init();

    crash_global_init();
    success = session_manager_init(1200, 0, 0);
    assert(success);

    pool = pool_new_libc(nullptr, "root");
    tpool_init(pool);

    test_cookie_client(pool);

    pool_unref(pool);
    tpool_deinit();
    pool_commit();
    pool_recycler_clear();

    session_manager_deinit();
    crash_global_deinit();

    event_base_free(event_base);
}
