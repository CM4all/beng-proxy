#include "widget.h"
#include "widget-http.h"
#include "strmap.h"
#include "growing-buffer.h"
#include "header-parser.h"
#include "tpool.h"
#include "get.h"
#include "http-response.h"
#include "processor.h"
#include "async.h"
#include "fcache.h"
#include "transformation.h"

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
int global_fcgi_stock;
int global_http_cache;
int global_tcp_stock;
const struct widget_class root_widget_class;

static unsigned test_id;
static bool got_request, got_response;

void
processor_new(__attr_unused pool_t pool, __attr_unused http_status_t status,
              __attr_unused struct strmap *headers,
              __attr_unused istream_t istream,
              __attr_unused struct widget *widget,
              __attr_unused struct processor_env *env,
              __attr_unused unsigned options,
              const struct http_response_handler *handler,
              void *handler_ctx,
              __attr_unused struct async_operation_ref *async_ref)
{
    http_response_handler_direct_abort(handler, handler_ctx);
}

struct filter_cache;

void
filter_cache_request(__attr_unused struct filter_cache *cache,
                     __attr_unused pool_t pool,
                     __attr_unused const struct resource_address *address,
                     __attr_unused const char *source_id,
                     __attr_unused http_status_t status,
                     __attr_unused struct strmap *headers,
                     __attr_unused istream_t body,
                     const struct http_response_handler *handler,
                     void *handler_ctx,
                     __attr_unused struct async_operation_ref *async_ref)
{
    http_response_handler_direct_abort(handler, handler_ctx);
}

struct http_cache;
struct tcp_stock;
struct fcgi_stock;
struct hstock;

void
resource_get(__attr_unused struct http_cache *cache,
             __attr_unused struct hstock *tcp_stock,
             __attr_unused struct hstock *fcgi_stock,
             __attr_unused struct hstock *delegate_stock,
             pool_t pool,
             http_method_t method,
             __attr_unused const struct resource_address *address,
             http_status_t status, struct strmap *headers, istream_t body,
             const struct http_response_handler *handler,
             void *handler_ctx,
             __attr_unused struct async_operation_ref *async_ref)
{
    struct strmap *response_headers = strmap_new(pool, 16);
    istream_t response_body = istream_null_new(pool);
    const char *p;

    assert(!got_request);
    assert(method == HTTP_METHOD_GET);
    assert(body == NULL);

    got_request = true;

    if (body != NULL)
        istream_close(body);

    switch (test_id) {
    case 0:
        p = strmap_get(headers, "cookie");
        assert(p == NULL);

        /* set one cookie */
        strmap_add(response_headers, "set-cookie", "foo=bar");
        break;

    case 1:
        /* is the one cookie present? */
        p = strmap_get(headers, "cookie");
        assert(p != NULL);
        assert(strcmp(p, "foo=bar") == 0);

        /* add 2 more cookies */
        strmap_add(response_headers, "set-cookie", "a=b, c=d");
        break;

    case 2:
        /* are 3 cookies present? */
        p = strmap_get(headers, "cookie");
        assert(p != NULL);
        assert(strcmp(p, "c=d; a=b; foo=bar") == 0);

        /* set two cookies in two headers */
        strmap_add(response_headers, "set-cookie", "e=f");
        strmap_add(response_headers, "set-cookie", "g=h");
        break;

    case 3:
        /* check for 5 cookies */
        p = strmap_get(headers, "cookie");
        assert(p != NULL);
        assert(strcmp(p, "g=h; e=f; c=d; a=b; foo=bar") == 0);
        break;
    }

    http_response_handler_direct_response(handler, handler_ctx,
                                          status, response_headers,
                                          response_body);
}

static void
my_http_response(http_status_t status, __attr_unused struct strmap *headers,
                 istream_t body, __attr_unused void *ctx)
{
    assert(!got_response);
    assert(status == 200);
    assert(body != NULL);

    istream_close(body);

    got_response = true;
}

static void __attr_noreturn
my_http_abort(__attr_unused void *ctx)
{
    assert(false);
}

static const struct http_response_handler my_http_response_handler = {
    .response = my_http_response,
    .abort = my_http_abort,
};

static void
test_cookie_client(pool_t pool)
{
    static struct uri_with_address address = {
        .uri = "http://foo/bar/",
    };
    static const struct transformation_view view;
    static const struct widget_class cls = {
        .address = {
            .type = RESOURCE_ADDRESS_HTTP,
            .u = {
                .http = &address,
            },
        },
        .views = &view,
        .stateful = true,

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
    };
    struct widget widget;
    struct session *session;
    struct processor_env env;
    struct async_operation_ref async_ref;

    session = session_new();

    env.local_host = "localhost";
    env.remote_host = "localhost";
    env.request_headers = strmap_new(pool, 16);
    env.session_id = session->id;
    session_put(session);

    widget_init(&widget, pool, &cls);
    widget.from_request.proxy = true;

    for (test_id = 0; test_id < 4; ++test_id) {
        got_request = false;
        got_response = false;

        widget_http_request(pool, &widget, &env,
                            &my_http_response_handler, NULL,
                            &async_ref);

        assert(got_request);
        assert(got_response);
    }
}

int main(int argc, char **argv) {
    struct event_base *event_base;
    bool success;
    pool_t pool;

    (void)argc;
    (void)argv;

    event_base = event_init();

    success = session_manager_init();
    assert(success);

    pool = pool_new_libc(NULL, "root");
    tpool_init(pool);

    test_cookie_client(pool);

    pool_unref(pool);
    tpool_deinit();
    pool_commit();
    pool_recycler_clear();

    session_manager_deinit();

    event_base_free(event_base);
}
