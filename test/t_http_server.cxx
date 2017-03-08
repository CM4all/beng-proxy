#include "http_server/http_server.hxx"
#include "http_server/Request.hxx"
#include "http_server/Handler.hxx"
#include "http_headers.hxx"
#include "direct.hxx"
#include "RootPool.hxx"
#include "pool.hxx"
#include "istream/istream.hxx"
#include "istream/istream_catch.hxx"
#include "fb_pool.hxx"
#include "io/FileDescriptor.hxx"
#include "event/Loop.hxx"

#include <stdio.h>
#include <stdlib.h>

struct Instance final : HttpServerConnectionHandler {
    struct pool *pool;

    HttpServerConnection *connection = nullptr;

    explicit Instance(struct pool &_pool)
        :pool(pool_new_libc(&_pool, "catch")) {}

    ~Instance() {
        CheckCloseConnection();
    }

    void CloseConnection() {
        http_server_connection_close(connection);
        connection = nullptr;
    }

    void CheckCloseConnection() {
        if (connection != nullptr)
            CloseConnection();
    }

    /* virtual methods from class HttpServerConnectionHandler */
    void HandleHttpRequest(HttpServerRequest &request,
                           CancellablePointer &cancel_ptr) override;

    void LogHttpRequest(HttpServerRequest &,
                        http_status_t, int64_t,
                        uint64_t, uint64_t) override {}

    void HttpConnectionError(GError *error) override;
    void HttpConnectionClosed() override;
};

static GError *
catch_callback(GError *error, gcc_unused void *ctx)
{
    fprintf(stderr, "%s\n", error->message);
    g_error_free(error);
    return nullptr;
}

void
Instance::HandleHttpRequest(HttpServerRequest &request,
                            gcc_unused CancellablePointer &cancel_ptr)
{
    http_server_response(&request, HTTP_STATUS_OK, HttpHeaders(request.pool),
                         istream_catch_new(&request.pool, *request.body,
                                           catch_callback, nullptr));

    CloseConnection();
}

void
Instance::HttpConnectionError(GError *error)
{
    connection = nullptr;

    g_printerr("%s\n", error->message);
    g_error_free(error);
}

void
Instance::HttpConnectionClosed()
{
    connection = nullptr;
}

static void
test_catch(EventLoop &event_loop, struct pool *_pool)
{
    FileDescriptor client_socket, server_socket;
    if (!FileDescriptor::CreateSocketPair(AF_LOCAL, SOCK_STREAM, 0,
                                          client_socket, server_socket)) {
        perror("socketpair() failed");
        exit(EXIT_FAILURE);
    }

    static constexpr char request[] =
        "POST / HTTP/1.1\r\nContent-Length: 1024\r\n\r\nfoo";
    send(client_socket.Get(), request, sizeof(request) - 1, 0);

    Instance instance(*_pool);
    instance.connection =
        http_server_connection_new(instance.pool, event_loop,
                                   server_socket.Get(), FdType::FD_SOCKET,
                                   nullptr, nullptr,
                                   nullptr, nullptr,
                                   true, instance);
    pool_unref(instance.pool);

    event_loop.Dispatch();

    client_socket.Close();
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    direct_global_init();
    const ScopeFbPoolInit fb_pool_init;
    EventLoop event_loop;

    test_catch(event_loop, RootPool());
}
