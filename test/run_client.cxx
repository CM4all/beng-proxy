#include "ajp/ajp_client.hxx"
#include "strmap.hxx"
#include "http_client.hxx"
#include "http_headers.hxx"
#include "http_response.hxx"
#include "lease.hxx"
#include "direct.hxx"
#include "istream/istream_file.hxx"
#include "istream/istream_pipe.hxx"
#include "istream/istream.hxx"
#include "istream/sink_fd.hxx"
#include "direct.hxx"
#include "tpool.hxx"
#include "RootPool.hxx"
#include "fb_pool.hxx"
#include "ssl/ssl_init.hxx"
#include "ssl/ssl_client.hxx"
#include "system/SetupProcess.hxx"
#include "net/ConnectSocket.hxx"
#include "net/SocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "event/Event.hxx"
#include "event/ShutdownListener.hxx"
#include "util/Cancellable.hxx"

#include <inline/compiler.h>
#include <socket/resolver.h>
#include <socket/util.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>

struct parsed_url {
    enum {
        HTTP, HTTPS, AJP,
    } protocol;

    char *host;

    int default_port;

    const char *uri;
};

static bool
parse_url(struct parsed_url *dest, const char *url)
{
    assert(dest != nullptr);
    assert(url != nullptr);

    if (memcmp(url, "ajp://", 6) == 0) {
        url += 6;
        dest->protocol = parsed_url::AJP;
        dest->default_port = 8009;
    } else if (memcmp(url, "http://", 7) == 0) {
        url += 7;
        dest->protocol = parsed_url::HTTP;
        dest->default_port = 80;
    } else if (memcmp(url, "https://", 8) == 0) {
        url += 8;
        dest->protocol = parsed_url::HTTPS;
        dest->default_port = 443;
    } else
        return false;

    dest->uri = strchr(url, '/');
    if (dest->uri == nullptr || dest->uri == url)
        return false;

    dest->host = g_strndup(url, dest->uri - url);
    return true;
}

struct Context final : ConnectSocketHandler, Lease, HttpResponseHandler {
    EventLoop event_loop;

    struct pool *pool;

    struct parsed_url url;

    ShutdownListener shutdown_listener;

    CancellablePointer cancel_ptr;

    http_method_t method;
    Istream *request_body;

    SocketDescriptor fd;
    bool idle, reuse, aborted;
    http_status_t status;

    SinkFd *body;
    bool body_eof, body_abort, body_closed;

    Context()
        :shutdown_listener(event_loop, BIND_THIS_METHOD(ShutdownCallback)) {}

    void ShutdownCallback();

    /* virtual methods from class ConnectSocketHandler */
    void OnSocketConnectSuccess(SocketDescriptor &&fd) override;
    void OnSocketConnectError(GError *error) override;

    /* virtual methods from class Lease */
    void ReleaseLease(bool _reuse) override {
        assert(!idle);
        assert(fd.IsDefined());

        idle = true;
        reuse = _reuse;

        fd.Close();
    }

    /* virtual methods from class HttpResponseHandler */
    void OnHttpResponse(http_status_t status, StringMap &&headers,
                        Istream *body) override;
    void OnHttpError(GError *error) override;
};

void
Context::ShutdownCallback()
{
    if (body != nullptr) {
        sink_fd_close(body);
        body = nullptr;
        body_abort = true;
    } else {
        aborted = true;
        cancel_ptr.Cancel();
    }
}

/*
 * istream handler
 *
 */

static void
my_sink_fd_input_eof(void *ctx)
{
    auto *c = (Context *)ctx;

    c->body = nullptr;
    c->body_eof = true;

    c->shutdown_listener.Disable();
}

static void
my_sink_fd_input_error(GError *error, void *ctx)
{
    auto *c = (Context *)ctx;

    g_printerr("%s\n", error->message);
    g_error_free(error);

    c->body = nullptr;
    c->body_abort = true;

    c->shutdown_listener.Disable();
}

static bool
my_sink_fd_send_error(int error, void *ctx)
{
    auto *c = (Context *)ctx;

    g_printerr("%s\n", g_strerror(error));

    c->body = nullptr;
    c->body_abort = true;

    c->shutdown_listener.Disable();
    return true;
}

static constexpr SinkFdHandler my_sink_fd_handler = {
    .input_eof = my_sink_fd_input_eof,
    .input_error = my_sink_fd_input_error,
    .send_error = my_sink_fd_send_error,
};


/*
 * http_response_handler
 *
 */

void
Context::OnHttpResponse(http_status_t _status, gcc_unused StringMap &&headers,
                        Istream *_body)
{
    status = _status;

    if (_body != nullptr) {
        _body = istream_pipe_new(pool, *_body, nullptr);
        body = sink_fd_new(*pool, *_body, 1, guess_fd_type(1),
                           my_sink_fd_handler, this);
        _body->Read();
    } else {
        body_eof = true;
        shutdown_listener.Disable();
    }
}

void
Context::OnHttpError(GError *error)
{
    g_printerr("%s\n", error->message);
    g_error_free(error);

    aborted = true;
}


/*
 * client_socket_handler
 *
 */

void
Context::OnSocketConnectSuccess(SocketDescriptor &&new_fd)
{
    fd = std::move(new_fd);

    StringMap headers(*pool);
    headers.Add("host", url.host);

    switch (url.protocol) {
    case parsed_url::AJP:
        ajp_client_request(*pool, event_loop,
                           fd.Get(), FdType::FD_TCP,
                           *this,
                           "http", "127.0.0.1", "localhost",
                           "localhost", 80, false,
                           method, url.uri, headers, request_body,
                           *this,
                           cancel_ptr);
        break;

    case parsed_url::HTTP:
        http_client_request(*pool, event_loop,
                            fd.Get(), FdType::FD_TCP,
                            *this,
                            "localhost",
                            nullptr, nullptr,
                            method, url.uri,
                            HttpHeaders(std::move(headers)),
                            request_body, false,
                            *this,
                            cancel_ptr);
        break;

    case parsed_url::HTTPS: {
        GError *error = nullptr;
        void *filter_ctx = ssl_client_create(pool, event_loop,
                                             url.host,
                                             &error);
        if (filter_ctx == nullptr) {
            g_printerr("%s\n", error->message);
            g_error_free(error);

            aborted = true;

            if (request_body != nullptr)
                request_body->CloseUnused();

            shutdown_listener.Disable();
            return;
        }

        auto filter = &ssl_client_get_filter();
        http_client_request(*pool, event_loop,
                            fd.Get(), FdType::FD_TCP,
                            *this,
                            "localhost",
                            filter, filter_ctx,
                            method, url.uri,
                            HttpHeaders(std::move(headers)),
                            request_body, false,
                            *this,
                            cancel_ptr);
        break;
    }
    }
}

void
Context::OnSocketConnectError(GError *error)
{
    g_printerr("%s\n", error->message);
    g_error_free(error);

    aborted = true;

    if (request_body != nullptr)
        request_body->CloseUnused();

    shutdown_listener.Disable();
}

/*
 * main
 *
 */

int
main(int argc, char **argv)
{
    Context ctx;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: run-ajp-client URL [BODY]\n");
        return EXIT_FAILURE;
    }

    if (!parse_url(&ctx.url, argv[1])) {
        fprintf(stderr, "Invalid or unsupported URL.\n");
        return EXIT_FAILURE;
    }

    const ScopeSslGlobalInit ssl_init;
    ssl_client_init();

    direct_global_init();

    /* connect socket */

    struct addrinfo hints, *ai;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;

    int ret = socket_resolve_host_port(ctx.url.host, ctx.url.default_port,
                                       &hints, &ai);
    if (ret != 0) {
        fprintf(stderr, "Failed to resolve host name\n");
        return EXIT_FAILURE;
    }

    /* initialize */

    SetupProcess();

    fb_pool_init(ctx.event_loop, false);

    ctx.shutdown_listener.Enable();

    RootPool root_pool;

    struct pool *pool = pool_new_linear(root_pool, "test", 8192);
    ctx.pool = pool;

    /* open request body */

    if (argc >= 3) {
        struct stat st;

        ret = stat(argv[2], &st);
        if (ret < 0) {
            fprintf(stderr, "Failed to stat %s: %s\n",
                    argv[2], strerror(errno));
            return EXIT_FAILURE;
        }

        ctx.method = HTTP_METHOD_POST;

        GError *error = nullptr;
        ctx.request_body = istream_file_new(ctx.event_loop, *pool,
                                            argv[2], st.st_size, &error);
        if (ctx.request_body == nullptr) {
            fprintf(stderr, "%s\n", error->message);
            g_error_free(error);
            return EXIT_FAILURE;
        }
    } else {
        ctx.method = HTTP_METHOD_GET;
        ctx.request_body = nullptr;
    }

    /* connect */

    client_socket_new(ctx.event_loop, *pool,
                      ai->ai_family, ai->ai_socktype, ai->ai_protocol,
                      false,
                      SocketAddress::Null(),
                      SocketAddress(ai->ai_addr, ai->ai_addrlen),
                      30,
                      ctx, ctx.cancel_ptr);
    freeaddrinfo(ai);

    /* run test */

    ctx.event_loop.Dispatch();

    assert(ctx.body_eof || ctx.body_abort || ctx.aborted);

    fprintf(stderr, "reuse=%d\n", ctx.reuse);

    /* cleanup */

    pool_unref(pool);
    pool_commit();

    fb_pool_deinit();

    ssl_client_deinit();

    g_free(ctx.url.host);

    return ctx.body_eof ? EXIT_SUCCESS : EXIT_FAILURE;
}
