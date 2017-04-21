#include "tconstruct.hxx"
#include "widget.hxx"
#include "widget_class.hxx"
#include "widget_http.hxx"
#include "widget_lookup.hxx"
#include "http_address.hxx"
#include "strmap.hxx"
#include "header_parser.hxx"
#include "PInstance.hxx"
#include "ResourceLoader.hxx"
#include "http_response.hxx"
#include "processor.hxx"
#include "css_processor.hxx"
#include "text_processor.hxx"
#include "penv.hxx"
#include "translation/Transformation.hxx"
#include "crash.hxx"
#include "istream/istream.hxx"
#include "istream/istream_null.hxx"
#include "istream/istream_pipe.hxx"
#include "session_manager.hxx"
#include "session.hxx"
#include "suffix_registry.hxx"
#include "address_suffix_registry.hxx"
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

struct request {
    bool cached;
    http_method_t method;
    const char *uri;
    const char *request_headers;

    http_status_t status;
    const char *response_headers;
    const char *response_body;
};

static unsigned test_id;
static bool got_request, got_response;

bool
processable(gcc_unused const StringMap &headers)
{
    return false;
}

Istream *
processor_process(gcc_unused struct pool &pool, Istream &istream,
                  gcc_unused Widget &widget,
                  gcc_unused struct processor_env &env,
                  gcc_unused unsigned options)
{
    return &istream;
}

void
processor_lookup_widget(gcc_unused struct pool &pool,
                        gcc_unused Istream &istream,
                        gcc_unused Widget &widget,
                        gcc_unused const char *id,
                        gcc_unused struct processor_env &env,
                        gcc_unused unsigned options,
                        WidgetLookupHandler &handler,
                        gcc_unused CancellablePointer &cancel_ptr)
{
    handler.WidgetNotFound();
}

Istream *
css_processor(gcc_unused struct pool &pool, Istream &stream,
              gcc_unused Widget &widget,
              gcc_unused struct processor_env &env,
              gcc_unused unsigned options)
{
    return &stream;
}

bool
text_processor_allowed(gcc_unused const StringMap &headers)
{
    return false;
}

Istream *
text_processor(gcc_unused struct pool &pool, Istream &stream,
               gcc_unused const Widget &widget,
               gcc_unused const struct processor_env &env)
{
    return &stream;
}

bool
suffix_registry_lookup(gcc_unused struct pool &pool,
                       gcc_unused struct tcache &translate_cache,
                       gcc_unused const ResourceAddress &address,
                       gcc_unused const SuffixRegistryHandler &handler,
                       gcc_unused void *ctx,
                       gcc_unused CancellablePointer &cancel_ptr)
{
    return false;
}

struct tcache *global_translate_cache;

class Stock;
Stock *global_pipe_stock;

Istream *
istream_pipe_new(gcc_unused struct pool *pool, Istream &input,
                 gcc_unused Stock *pipe_stock)
{
    return &input;
}

class MyResourceLoader final : public ResourceLoader {
public:
    /* virtual methods from class ResourceLoader */
    void SendRequest(struct pool &pool,
                     sticky_hash_t,
                     http_method_t method,
                     const ResourceAddress &address,
                     http_status_t status, StringMap &&headers,
                     Istream *body, const char *body_etag,
                     HttpResponseHandler &handler,
                     CancellablePointer &cancel_ptr) override;
};

void
MyResourceLoader::SendRequest(struct pool &pool,
                              sticky_hash_t,
                              http_method_t method,
                              gcc_unused const ResourceAddress &address,
                              gcc_unused http_status_t status,
                              StringMap &&headers,
                              Istream *body, gcc_unused const char *body_etag,
                              HttpResponseHandler &handler,
                              gcc_unused CancellablePointer &cancel_ptr)
{
    StringMap response_headers(pool);
    Istream *response_body = istream_null_new(&pool);
    const char *p;

    assert(!got_request);
    assert(method == HTTP_METHOD_GET);
    assert(body == nullptr);

    got_request = true;

    if (body != nullptr)
        body->CloseUnused();

    switch (test_id) {
    case 0:
        p = headers.Get("cookie");
        assert(p == nullptr);

        /* set one cookie */
        response_headers.Add("set-cookie", "foo=bar");
        break;

    case 1:
        /* is the one cookie present? */
        p = headers.Get("cookie");
        assert(p != nullptr);
        assert(strcmp(p, "foo=bar") == 0);

        /* add 2 more cookies */
        response_headers.Add("set-cookie", "a=b, c=d");
        break;

    case 2:
        /* are 3 cookies present? */
        p = headers.Get("cookie");
        assert(p != nullptr);
        assert(strcmp(p, "c=d; a=b; foo=bar") == 0);

        /* set two cookies in two headers */
        response_headers.Add("set-cookie", "e=f");
        response_headers.Add("set-cookie", "g=h");
        break;

    case 3:
        /* check for 5 cookies */
        p = headers.Get("cookie");
        assert(p != nullptr);
        assert(strcmp(p, "g=h; e=f; c=d; a=b; foo=bar") == 0);
        break;
    }

    handler.InvokeResponse(HTTP_STATUS_OK,
                           std::move(response_headers),
                           response_body);
}

struct Context final : HttpResponseHandler {
    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status, StringMap &&headers,
                        Istream *body) override;
    void OnHttpError(GError *error) override;
};

void
Context::OnHttpResponse(http_status_t status, gcc_unused StringMap &&headers,
                        Istream *body)
{
    assert(!got_response);
    assert(status == 200);
    assert(body != nullptr);

    body->CloseUnused();

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
test_cookie_client(struct pool *pool)
{
    const auto address = MakeHttpAddress("/bar/").Host("foo");
    WidgetClass cls;
    cls.Init();
    cls.views.address = address;
    cls.stateful = true;

    CancellablePointer cancel_ptr;

    auto *session = session_new();

    MyResourceLoader resource_loader;
    struct processor_env env;
    env.resource_loader = &resource_loader;
    env.local_host = "localhost";
    env.remote_host = "localhost";
    env.request_headers = strmap_new(pool);
    env.session_id = session->id;
    env.realm = "foo";
    session_put(session);

    Widget widget(*pool, &cls);

    for (test_id = 0; test_id < 4; ++test_id) {
        got_request = false;
        got_response = false;

        Context context;
        widget_http_request(*pool, widget, env,
                            context, cancel_ptr);

        assert(got_request);
        assert(got_response);
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    PInstance instance;

    crash_global_init();
    session_manager_init(instance.event_loop, std::chrono::minutes(30), 0, 0);

    test_cookie_client(instance.root_pool);

    session_manager_deinit();
    crash_global_deinit();
}
