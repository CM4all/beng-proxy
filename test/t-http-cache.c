#include "http-cache.h"
#include "http-cache-internal.h"
#include "resource-loader.h"
#include "resource-address.h"
#include "growing-buffer.h"
#include "header-parser.h"
#include "strmap.h"
#include "http-response.h"
#include "async.h"
#include "tpool.h"

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
http_cache_memcached_flush(G_GNUC_UNUSED pool_t pool,
                           G_GNUC_UNUSED struct memcached_stock *stock,
                           G_GNUC_UNUSED http_cache_memcached_flush_t callback,
                           G_GNUC_UNUSED void *callback_ctx,
                           G_GNUC_UNUSED struct async_operation_ref *async_ref)
{
}

void
http_cache_memcached_get(G_GNUC_UNUSED pool_t pool,
                         G_GNUC_UNUSED struct memcached_stock *stock,
                         G_GNUC_UNUSED pool_t background_pool,
                         G_GNUC_UNUSED struct background_manager *background,
                         G_GNUC_UNUSED const char *uri,
                         G_GNUC_UNUSED struct strmap *request_headers,
                         G_GNUC_UNUSED http_cache_memcached_get_t callback,
                         G_GNUC_UNUSED void *callback_ctx,
                         G_GNUC_UNUSED struct async_operation_ref *async_ref)
{
}

void
http_cache_memcached_put(G_GNUC_UNUSED pool_t pool,
                         G_GNUC_UNUSED struct memcached_stock *stock,
                         G_GNUC_UNUSED pool_t background_pool,
                         G_GNUC_UNUSED struct background_manager *background,
                         G_GNUC_UNUSED const char *uri,
                         G_GNUC_UNUSED const struct http_cache_info *info,
                         G_GNUC_UNUSED struct strmap *request_headers,
                         G_GNUC_UNUSED http_status_t status,
                         G_GNUC_UNUSED struct strmap *response_headers,
                         G_GNUC_UNUSED istream_t value,
                         G_GNUC_UNUSED http_cache_memcached_put_t put,
                         G_GNUC_UNUSED void *callback_ctx,
                         G_GNUC_UNUSED struct async_operation_ref *async_ref)
{
}

void
http_cache_memcached_remove_uri(G_GNUC_UNUSED struct memcached_stock *stock,
                                G_GNUC_UNUSED pool_t background_pool,
                                G_GNUC_UNUSED struct background_manager *background,
                                G_GNUC_UNUSED const char *uri)
{
}

void
http_cache_memcached_remove_uri_match(G_GNUC_UNUSED struct memcached_stock *stock,
                                      G_GNUC_UNUSED pool_t background_pool,
                                      G_GNUC_UNUSED struct background_manager *background,
                                      G_GNUC_UNUSED const char *uri,
                                      G_GNUC_UNUSED struct strmap *headers)
{
}

static struct strmap *
parse_headers(pool_t pool, const char *raw)
{
    struct growing_buffer *gb;
    struct strmap *headers;

    if (raw == NULL)
        return NULL;

    gb = growing_buffer_new(pool, 512);
    headers = strmap_new(pool, 64);
    growing_buffer_write_string(gb, raw);
    header_parse_buffer(pool, headers, gb);

    return headers;
}

static struct strmap *
parse_request_headers(pool_t pool, const struct request *request)
{
    return parse_headers(pool, request->request_headers);
}

static struct strmap *
parse_response_headers(pool_t pool, const struct request *request)
{
    return parse_headers(pool, request->response_headers);
}

void
resource_loader_request(__attr_unused struct resource_loader *rl, pool_t pool,
                        http_method_t method,
                        __attr_unused const struct resource_address *address,
                        __attr_unused http_status_t status, struct strmap *headers,
                        istream_t body,
                        const struct http_response_handler *handler,
                        void *handler_ctx,
                        __attr_unused struct async_operation_ref *async_ref)
{
    const struct request *request = &requests[current_request];
    struct strmap *expected_rh;
    struct strmap *response_headers;
    istream_t response_body;

    assert(!got_request);
    assert(!got_response);
    assert(method == request->method);

    got_request = true;

    validated = strmap_get_checked(headers, "if-modified-since") != NULL;

    expected_rh = parse_request_headers(pool, request);
    if (expected_rh != NULL) {
        const struct strmap_pair *pair;

        assert(headers != NULL);

        strmap_rewind(expected_rh);
        while ((pair = strmap_next(expected_rh)) != NULL) {
            const char *value = strmap_get_checked(headers, pair->key);
            assert(value != NULL);
            assert(strcmp(value, pair->value) == 0);
        }
    }

    if (body != NULL)
        istream_close(body);

    if (request->response_headers != NULL) {
        struct growing_buffer *gb = growing_buffer_new(pool, 512);
        growing_buffer_write_string(gb, request->response_headers);

        response_headers = strmap_new(pool, 64);
        header_parse_buffer(pool, response_headers, gb);
    } else
        response_headers = NULL;

    if (request->response_body != NULL)
        response_body = istream_string_new(pool, request->response_body);
    else
        response_body = NULL;

    http_response_handler_direct_response(handler, handler_ctx,
                                          request->status, response_headers,
                                          response_body);
}

static size_t
my_response_body_data(__attr_unused const void *data, size_t length, __attr_unused void *ctx)
{
    const struct request *request = &requests[current_request];

    assert(body_read + length <= strlen(request->response_body));
    assert(memcmp(request->response_body + body_read, data, length) == 0);

    body_read += length;
    return length;
}

static void
my_response_body_eof(__attr_unused void *ctx)
{
    eof = true;
}

static void __attr_noreturn
my_response_body_abort(__attr_unused void *ctx)
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
                 istream_t body, void *ctx)
{
    pool_t pool = ctx;
    const struct request *request = &requests[current_request];
    struct strmap *expected_rh;

    assert(status == request->status);

    expected_rh = parse_response_headers(pool, request);
    if (expected_rh != NULL) {
        const struct strmap_pair *pair;

        assert(headers != NULL);

        strmap_rewind(expected_rh);
        while ((pair = strmap_next(expected_rh)) != NULL) {
            const char *value = strmap_get(headers, pair->key);
            assert(value != NULL);
            assert(strcmp(value, pair->value) == 0);
        }
    }

    if (body != NULL) {
        body_read = 0;
        istream_handler_set(body, &my_response_body_handler, NULL, 0);
        istream_read(body);
    }

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
run_cache_test(pool_t root_pool, unsigned num, bool cached)
{
    const struct request *request = &requests[num];
    pool_t pool = pool_new_linear(root_pool, "t_http_cache", 8192);
    struct uri_with_address uwa = {
        .pool = pool,
        .uri = request->uri,
    };
    const struct resource_address address = {
        .type = RESOURCE_ADDRESS_HTTP,
        .u = {
            .http = &uwa,
        },
    };
    struct strmap *headers;
    istream_t body;
    struct async_operation_ref async_ref;

    current_request = num;

    if (request->request_headers != NULL) {
        struct growing_buffer *gb = growing_buffer_new(pool, 512);

        headers = strmap_new(pool, 64);
        growing_buffer_write_string(gb, request->request_headers);
        header_parse_buffer(pool, headers, gb);
    } else
        headers = NULL;

    body = NULL;

    got_request = cached;
    got_response = false;

    http_cache_request(cache, pool, request->method, &address,
                       headers, body,
                       &my_http_response_handler, pool,
                       &async_ref);
    pool_unref(pool);

    assert(got_request);
    assert(got_response);
}

int main(int argc, char **argv) {
    struct event_base *event_base;
    pool_t pool;

    (void)argc;
    (void)argv;

    event_base = event_init();

    pool = pool_new_libc(NULL, "root");
    tpool_init(pool);
    cache = http_cache_new(pool, 1024 * 1024, NULL, NULL);

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
