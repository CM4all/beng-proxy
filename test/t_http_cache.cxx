#include "tconstruct.hxx"
#include "http_cache.hxx"
#include "http_cache_memcached.hxx"
#include "ResourceLoader.hxx"
#include "ResourceAddress.hxx"
#include "http_address.hxx"
#include "GrowingBuffer.hxx"
#include "header_parser.hxx"
#include "strmap.hxx"
#include "http_response.hxx"
#include "RootPool.hxx"
#include "fb_pool.hxx"
#include "istream/istream.hxx"
#include "istream/istream_string.hxx"
#include "event/Event.hxx"
#include "util/Cancellable.hxx"

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
                           gcc_unused MemachedStock &stock,
                           gcc_unused http_cache_memcached_flush_t callback,
                           gcc_unused void *callback_ctx,
                           gcc_unused CancellablePointer &cancel_ptr)
{
}

void
http_cache_memcached_get(gcc_unused struct pool &pool,
                         gcc_unused MemachedStock &stock,
                         gcc_unused struct pool &background_pool,
                         gcc_unused BackgroundManager &background,
                         gcc_unused const char *uri,
                         gcc_unused StringMap &request_headers,
                         gcc_unused http_cache_memcached_get_t callback,
                         gcc_unused void *callback_ctx,
                         gcc_unused CancellablePointer &cancel_ptr)
{
}

void
http_cache_memcached_put(gcc_unused struct pool &pool,
                         gcc_unused MemachedStock &stock,
                         gcc_unused struct pool &background_pool,
                         gcc_unused BackgroundManager &background,
                         gcc_unused const char *uri,
                         gcc_unused const HttpCacheResponseInfo &info,
                         gcc_unused const StringMap &request_headers,
                         gcc_unused http_status_t status,
                         gcc_unused const StringMap *response_headers,
                         gcc_unused Istream *value,
                         gcc_unused http_cache_memcached_put_t put,
                         gcc_unused void *callback_ctx,
                         gcc_unused CancellablePointer &cancel_ptr)
{
}

void
http_cache_memcached_remove_uri(gcc_unused MemachedStock &stock,
                                gcc_unused struct pool &background_pool,
                                gcc_unused BackgroundManager &background,
                                gcc_unused const char *uri)
{
}

void
http_cache_memcached_remove_uri_match(gcc_unused MemachedStock &stock,
                                      gcc_unused struct pool &background_pool,
                                      gcc_unused BackgroundManager &background,
                                      gcc_unused const char *uri,
                                      gcc_unused StringMap &headers)
{
}

static StringMap *
parse_headers(struct pool &pool, const char *raw)
{
    if (raw == NULL)
        return NULL;

    GrowingBuffer gb;
    StringMap *headers = strmap_new(&pool);
    gb.Write(raw);
    header_parse_buffer(pool, *headers, std::move(gb));

    return headers;
}

static StringMap *
parse_request_headers(struct pool &pool, const Request &request)
{
    return parse_headers(pool, request.request_headers);
}

static StringMap *
parse_response_headers(struct pool &pool, const Request &request)
{
    return parse_headers(pool, request.response_headers);
}

class MyResourceLoader final : public ResourceLoader {
public:
    /* virtual methods from class ResourceLoader */
    void SendRequest(struct pool &pool,
                     unsigned session_sticky,
                     http_method_t method,
                     const ResourceAddress &address,
                     http_status_t status, StringMap &&headers,
                     Istream *body, const char *body_etag,
                     HttpResponseHandler &handler,
                     CancellablePointer &cancel_ptr) override;
};

void
MyResourceLoader::SendRequest(struct pool &pool,
                              gcc_unused unsigned session_sticky,
                              http_method_t method,
                              gcc_unused const ResourceAddress &address,
                              gcc_unused http_status_t status,
                              StringMap &&headers,
                              Istream *body, gcc_unused const char *body_etag,
                              HttpResponseHandler &handler,
                              gcc_unused CancellablePointer &cancel_ptr)
{
    const auto *request = &requests[current_request];
    StringMap *expected_rh;
    Istream *response_body;

    assert(!got_request);
    assert(!got_response);
    assert(method == request->method);

    got_request = true;

    validated = headers.Get("if-modified-since") != nullptr;

    expected_rh = parse_request_headers(pool, *request);
    if (expected_rh != NULL) {
        for (const auto &i : headers) {
            const char *value = headers.Get(i.key);
            assert(value != NULL);
            assert(strcmp(value, i.value) == 0);
        }
    }

    if (body != NULL)
        body->CloseUnused();

    StringMap response_headers(pool);
    if (request->response_headers != NULL) {
        GrowingBuffer gb;
        gb.Write(request->response_headers);

        header_parse_buffer(pool, response_headers, std::move(gb));
    }

    if (request->response_body != NULL)
        response_body = istream_string_new(&pool, request->response_body);
    else
        response_body = NULL;

    handler.InvokeResponse(request->status,
                           std::move(response_headers),
                           response_body);
}

struct Context final : HttpResponseHandler {
    struct pool &pool;

    explicit Context(struct pool &_pool):pool(_pool) {}

    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status, StringMap &&headers,
                        Istream *body) override;
    void OnHttpError(GError *error) override;
};

void
Context::OnHttpResponse(http_status_t status, StringMap &&headers,
                        Istream *body)
{
    Request *request = &requests[current_request];
    StringMap *expected_rh;

    assert(status == request->status);

    expected_rh = parse_response_headers(pool, *request);
    if (expected_rh != NULL) {
        for (const auto &i : *expected_rh) {
            const char *value = headers.Get(i.key);
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

void gcc_noreturn
Context::OnHttpError(GError *error)
{
    g_printerr("%s\n", error->message);
    g_error_free(error);

    assert(false);
}

static void
run_cache_test(struct pool *root_pool, unsigned num, bool cached)
{
    const Request *request = &requests[num];
    struct pool *pool = pool_new_linear(root_pool, "t_http_cache", 8192);
    const auto uwa = MakeHttpAddress(request->uri).Host("foo");
    const ResourceAddress address(uwa);

    Istream *body;
    CancellablePointer cancel_ptr;

    current_request = num;

    StringMap headers(*pool);
    if (request->request_headers != NULL) {
        GrowingBuffer gb;
        gb.Write(request->request_headers);

        header_parse_buffer(*pool, headers, std::move(gb));
    }

    body = NULL;

    got_request = cached;
    got_response = false;

    Context context(*pool);
    http_cache_request(*cache, *pool, 0, request->method, address,
                       std::move(headers), body,
                       context, cancel_ptr);
    pool_unref(pool);

    assert(got_request);
    assert(got_response);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    const ScopeFbPoolInit fb_pool_init;
    EventLoop event_loop;

    RootPool pool;
    MyResourceLoader resource_loader;

    cache = http_cache_new(*pool, 1024 * 1024, nullptr,
                           event_loop, resource_loader);

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
