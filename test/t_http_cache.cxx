#include "tconstruct.hxx"
#include "http_cache.hxx"
#include "http_cache_memcached.hxx"
#include "resource_loader.hxx"
#include "ResourceAddress.hxx"
#include "http_address.hxx"
#include "growing_buffer.hxx"
#include "header_parser.hxx"
#include "strmap.hxx"
#include "http_response.hxx"
#include "async.hxx"
#include "RootPool.hxx"
#include "istream/istream.hxx"
#include "istream/istream_string.hxx"
#include "event/Event.hxx"

#include <inline/compiler.h>

#include <glib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

struct Request final : IstreamHandler {
    bool cached = false;
    http_method_t method = HTTP_METHOD_GET;
    const char *uri;
    const char *request_headers;

    http_status_t status = HTTP_STATUS_OK;
    const char *response_headers;
    const char *response_body;

    bool eof = false;
    size_t body_read = 0;

    Request(const char *_uri, const char *_request_headers,
            const char *_response_headers,
            const char *_response_body)
        :uri(_uri), request_headers(_request_headers),
         response_headers(_response_headers),
         response_body(_response_body) {}

    /* virtual methods from class IstreamHandler */
    size_t OnData(gcc_unused const void *data, size_t length) override {
        assert(body_read + length <= strlen(response_body));
        assert(memcmp(response_body + body_read, data, length) == 0);

        body_read += length;
        return length;
    }

    void OnEof() override {
        eof = true;
    }

    void OnError(GError *error) override {
        g_error_free(error);
        assert(false);
    }
};

#define DATE "Fri, 30 Jan 2009 10:53:30 GMT"
#define STAMP1 "Fri, 30 Jan 2009 08:53:30 GMT"
#define STAMP2 "Fri, 20 Jan 2009 08:53:30 GMT"
#define EXPIRES "Fri, 20 Jan 2029 08:53:30 GMT"

Request requests[] = {
    { "/foo", nullptr,
      "date: " DATE "\n"
      "last-modified: " STAMP1 "\n"
      "expires: " EXPIRES "\n"
      "vary: x-foo\n",
      "foo",
    },
    { "/foo", "x-foo: foo\n",
      "date: " DATE "\n"
      "last-modified: " STAMP2 "\n"
      "expires: " EXPIRES "\n"
      "vary: x-foo\n",
      "bar",
    },
    { "/query?string", nullptr,
      "date: " DATE "\n"
      "last-modified: " STAMP1 "\n",
      "foo",
    },
    { "/query?string2", nullptr,
      "date: " DATE "\n"
      "last-modified: " STAMP1 "\n"
      "expires: " EXPIRES "\n",
      "foo",
    },
};

static HttpCache *cache;
static unsigned current_request;
static bool got_request, got_response;
static bool validated;

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
                         gcc_unused const HttpCacheResponseInfo &info,
                         gcc_unused const struct strmap *request_headers,
                         gcc_unused http_status_t status,
                         gcc_unused const struct strmap *response_headers,
                         gcc_unused Istream *value,
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
parse_request_headers(struct pool *pool, const Request *request)
{
    return parse_headers(pool, request->request_headers);
}

static struct strmap *
parse_response_headers(struct pool *pool, const Request *request)
{
    return parse_headers(pool, request->response_headers);
}

void
resource_loader_request(gcc_unused struct resource_loader *rl, struct pool *pool,
                        gcc_unused unsigned session_sticky,
                        http_method_t method,
                        gcc_unused const ResourceAddress *address,
                        gcc_unused http_status_t status, struct strmap *headers,
                        Istream *body,
                        const struct http_response_handler *handler,
                        void *handler_ctx,
                        gcc_unused struct async_operation_ref *async_ref)
{
    const Request *request = &requests[current_request];
    struct strmap *expected_rh;
    struct strmap *response_headers;
    Istream *response_body;

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
        body->CloseUnused();

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

static void
my_http_response(http_status_t status, struct strmap *headers,
                 Istream *body, void *ctx)
{
    struct pool *pool = (struct pool *)ctx;
    Request *request = &requests[current_request];
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
        request->body_read = 0;
        body->SetHandler(*request);
        body->Read();
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
    const Request *request = &requests[num];
    struct pool *pool = pool_new_linear(root_pool, "t_http_cache", 8192);
    const auto uwa = MakeHttpAddress(request->uri).Host("foo");
    const ResourceAddress address(ResourceAddress::Type::HTTP, uwa);

    struct strmap *headers;
    Istream *body;
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
    (void)argc;
    (void)argv;

    EventLoop event_loop;

    RootPool pool;

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
}
