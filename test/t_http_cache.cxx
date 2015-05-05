#include "http_cache.hxx"
#include "http_cache_memcached.hxx"
#include "resource_loader.hxx"
#include "resource_address.hxx"
#include "http_address.hxx"
#include "growing_buffer.hxx"
#include "header_parser.hxx"
#include "strmap.hxx"
#include "http_response.hxx"
#include "async.hxx"
#include "tpool.hxx"
#include "istream.h"

#include <inline/compiler.h>

#include <glib.h>

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

#define DATE "Fri, 30 Jan 2009 10:53:30 GMT"
#define STAMP1 "Fri, 30 Jan 2009 08:53:30 GMT"
#define STAMP2 "Fri, 20 Jan 2009 08:53:30 GMT"
#define EXPIRES "Fri, 20 Jan 2029 08:53:30 GMT"

struct request requests[] = {
    { .method = HTTP_METHOD_GET,
      .uri = "/foo",
      .request_headers = NULL,
      .status = HTTP_STATUS_OK,
      .response_headers =
      "date: " DATE "\n"
      "last-modified: " STAMP1 "\n"
      "expires: " EXPIRES "\n"
      "vary: x-foo\n",
      .response_body = "foo",
    },
    { .method = HTTP_METHOD_GET,
      .uri = "/foo",
      .request_headers = "x-foo: foo\n",
      .status = HTTP_STATUS_OK,
      .response_headers =
      "date: " DATE "\n"
      "last-modified: " STAMP2 "\n"
      "expires: " EXPIRES "\n"
      "vary: x-foo\n",
      .response_body = "bar",
    },
    { .method = HTTP_METHOD_GET,
      .uri = "/query?string",
      .request_headers = NULL,
      .status = HTTP_STATUS_OK,
      .response_headers =
      "date: " DATE "\n"
      "last-modified: " STAMP1 "\n",
      .response_body = "foo",
    },
    { .method = HTTP_METHOD_GET,
      .uri = "/query?string2",
      .request_headers = NULL,
      .status = HTTP_STATUS_OK,
      .response_headers =
      "date: " DATE "\n"
      "last-modified: " STAMP1 "\n"
      "expires: " EXPIRES "\n",
      .response_body = "foo",
    },
};

static struct http_cache *cache;
static unsigned current_request;
static bool got_request, got_response;
static bool validated;
static bool eof;
static size_t body_read;

void
http_cache_memcached_flush(gcc_unused struct pool &pool,
                           gcc_unused struct memcached_stock &stock,
                           gcc_unused http_cache_memcached_flush_t callback,
                           gcc_unused void *callback_ctx,
                           gcc_unused struct async_operation_ref &async_ref)
{
}

void
http_cache_memcached_get(gcc_unused struct pool &pool,
                         gcc_unused struct memcached_stock &stock,
                         gcc_unused struct pool &background_pool,
                         gcc_unused BackgroundManager &background,
                         gcc_unused const char *uri,
                         gcc_unused struct strmap *request_headers,
                         gcc_unused http_cache_memcached_get_t callback,
                         gcc_unused void *callback_ctx,
                         gcc_unused struct async_operation_ref &async_ref)
{
}

void
http_cache_memcached_put(gcc_unused struct pool &pool,
                         gcc_unused struct memcached_stock &stock,
                         gcc_unused struct pool &background_pool,
                         gcc_unused BackgroundManager &background,
                         gcc_unused const char *uri,
                         gcc_unused const struct http_cache_response_info &info,
                         gcc_unused const struct strmap *request_headers,
                         gcc_unused http_status_t status,
                         gcc_unused const struct strmap *response_headers,
                         gcc_unused struct istream *value,
                         gcc_unused http_cache_memcached_put_t put,
                         gcc_unused void *callback_ctx,
                         gcc_unused struct async_operation_ref &async_ref)
{
}

void
http_cache_memcached_remove_uri(gcc_unused struct memcached_stock &stock,
                                gcc_unused struct pool &background_pool,
                                gcc_unused BackgroundManager &background,
                                gcc_unused const char *uri)
{
}

void
http_cache_memcached_remove_uri_match(gcc_unused struct memcached_stock &stock,
                                      gcc_unused struct pool &background_pool,
                                      gcc_unused BackgroundManager &background,
                                      gcc_unused const char *uri,
                                      gcc_unused struct strmap *headers)
{
}

static struct strmap *
parse_headers(struct pool *pool, const char *raw)
{
    GrowingBuffer *gb;

    if (raw == NULL)
        return NULL;

    gb = growing_buffer_new(pool, 512);
    struct strmap *headers = strmap_new(pool);
    growing_buffer_write_string(gb, raw);
    header_parse_buffer(pool, headers, gb);

    return headers;
}

static struct strmap *
parse_request_headers(struct pool *pool, const struct request *request)
{
    return parse_headers(pool, request->request_headers);
}

static struct strmap *
parse_response_headers(struct pool *pool, const struct request *request)
{
    return parse_headers(pool, request->response_headers);
}

void
resource_loader_request(gcc_unused struct resource_loader *rl, struct pool *pool,
                        gcc_unused unsigned session_sticky,
                        http_method_t method,
                        gcc_unused const struct resource_address *address,
                        gcc_unused http_status_t status, struct strmap *headers,
                        struct istream *body,
                        const struct http_response_handler *handler,
                        void *handler_ctx,
                        gcc_unused struct async_operation_ref *async_ref)
{
    const struct request *request = &requests[current_request];
    struct strmap *expected_rh;
    struct strmap *response_headers;
    struct istream *response_body;

    assert(!got_request);
    assert(!got_response);
    assert(method == request->method);

    got_request = true;

    validated = strmap_get_checked(headers, "if-modified-since") != NULL;

    expected_rh = parse_request_headers(pool, request);
    if (expected_rh != NULL) {
        assert(headers != NULL);

        for (const auto &i : *headers) {
            const char *value = strmap_get_checked(headers, i.key);
            assert(value != NULL);
            assert(strcmp(value, i.value) == 0);
        }
    }

    if (body != NULL)
        istream_close_unused(body);

    if (request->response_headers != NULL) {
        GrowingBuffer *gb = growing_buffer_new(pool, 512);
        growing_buffer_write_string(gb, request->response_headers);

        response_headers = strmap_new(pool);
        header_parse_buffer(pool, response_headers, gb);
    } else
        response_headers = NULL;

    if (request->response_body != NULL)
        response_body = istream_string_new(pool, request->response_body);
    else
        response_body = NULL;

    handler->InvokeResponse(handler_ctx, request->status, response_headers,
                            response_body);
}

static size_t
my_response_body_data(gcc_unused const void *data, size_t length,
                      gcc_unused void *ctx)
{
    const struct request *request = &requests[current_request];

    assert(body_read + length <= strlen(request->response_body));
    assert(memcmp(request->response_body + body_read, data, length) == 0);

    body_read += length;
    return length;
}

static void
my_response_body_eof(gcc_unused void *ctx)
{
    eof = true;
}

static void gcc_noreturn
my_response_body_abort(gcc_unused GError *error, gcc_unused void *ctx)
{
    assert(false);
}

static const struct istream_handler my_response_body_handler = {
    .data = my_response_body_data,
    .eof = my_response_body_eof,
    .abort = my_response_body_abort,
};

static void
my_http_response(http_status_t status, struct strmap *headers,
                 struct istream *body, void *ctx)
{
    struct pool *pool = (struct pool *)ctx;
    const struct request *request = &requests[current_request];
    struct strmap *expected_rh;

    assert(status == request->status);

    expected_rh = parse_response_headers(pool, request);
    if (expected_rh != NULL) {
        assert(headers != NULL);

        for (const auto &i : *expected_rh) {
            const char *value = headers->Get(i.key);
            assert(value != NULL);
            assert(strcmp(value, i.value) == 0);
        }
    }

    if (body != NULL) {
        body_read = 0;
        istream_handler_set(body, &my_response_body_handler, NULL, 0);
        istream_read(body);
    }

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
run_cache_test(struct pool *root_pool, unsigned num, bool cached)
{
    const struct request *request = &requests[num];
    struct pool *pool = pool_new_linear(root_pool, "t_http_cache", 8192);
    struct http_address uwa = {
        .scheme = URI_SCHEME_HTTP,
        .host_and_port = "foo",
        .path = request->uri,
    };
    const struct resource_address address = {
        .type = RESOURCE_ADDRESS_HTTP,
        .u = {
            .http = &uwa,
        },
    };
    struct strmap *headers;
    struct istream *body;
    struct async_operation_ref async_ref;

    current_request = num;

    if (request->request_headers != NULL) {
        GrowingBuffer *gb = growing_buffer_new(pool, 512);

        headers = strmap_new(pool);
        growing_buffer_write_string(gb, request->request_headers);
        header_parse_buffer(pool, headers, gb);
    } else
        headers = NULL;

    body = NULL;

    got_request = cached;
    got_response = false;

    http_cache_request(*cache, *pool, 0, request->method, address,
                       headers, body,
                       my_http_response_handler, pool,
                       async_ref);
    pool_unref(pool);

    assert(got_request);
    assert(got_response);
}

int main(int argc, char **argv) {
    struct event_base *event_base;
    struct pool *pool;

    (void)argc;
    (void)argv;

    event_base = event_init();

    pool = pool_new_libc(NULL, "root");
    tpool_init(pool);
    cache = http_cache_new(*pool, 1024 * 1024, nullptr,
                           *(struct resource_loader *)nullptr);

    /* request one resource, cold and warm cache */
    run_cache_test(pool, 0, false);
    run_cache_test(pool, 0, true);

    /* another resource, different header */
    run_cache_test(pool, 1, false);
    run_cache_test(pool, 1, true);

    /* see if the first resource is still cached */
    run_cache_test(pool, 0, true);

    /* see if the second resource is still cached */
    run_cache_test(pool, 1, true);

    /* query string: should not be cached */

    run_cache_test(pool, 2, false);

    validated = false;
    run_cache_test(pool, 2, false);
    assert(!validated);

    /* double check with a cacheable query string ("Expires" is
       set) */
    run_cache_test(pool, 3, false);
    run_cache_test(pool, 3, true);

    http_cache_close(cache);
    pool_unref(pool);
    tpool_deinit();
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
}
