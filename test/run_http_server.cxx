#include "http_server/http_server.hxx"
#include "http_server/Request.hxx"
#include "http_server/Handler.hxx"
#include "http_headers.hxx"
#include "duplex.hxx"
#include "direct.hxx"
#include "istream/sink_null.hxx"
#include "istream/istream_byte.hxx"
#include "istream/istream_delayed.hxx"
#include "istream/istream_head.hxx"
#include "istream/istream_hold.hxx"
#include "istream/istream_memory.hxx"
#include "istream/istream_zero.hxx"
#include "istream/istream.hxx"
#include "RootPool.hxx"
#include "pool.hxx"
#include "async.hxx"
#include "event/Base.hxx"
#include "event/TimerEvent.hxx"
#include "event/ShutdownListener.hxx"
#include "fb_pool.hxx"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Instance {
    EventBase event_base;

    struct async_operation operation;

    ShutdownListener shutdown_listener;

    enum class Mode {
        MODE_NULL,
        MIRROR,
        DUMMY,
        FIXED,
        HUGE_,
        HOLD,
    } mode;

    HttpServerConnection *connection;

    Istream *request_body;

    TimerEvent timer;

    Instance()
        :shutdown_listener(ShutdownCallback, this) {}

    static void ShutdownCallback(void *ctx);

    void Abort() {
        if (request_body != nullptr)
            request_body->CloseUnused();

        timer.Cancel();
    }
};

void
Instance::ShutdownCallback(void *ctx)
{
    Instance *c = (Instance *)ctx;

    http_server_connection_close(c->connection);
}

static void
timer_callback(gcc_unused int fd, gcc_unused short event, void *_ctx)
{
    Instance *ctx = (Instance *)_ctx;

    http_server_connection_close(ctx->connection);
    ctx->shutdown_listener.Disable();
}

/*
 * http_server handler
 *
 */

static void
my_request(struct http_server_request *request, void *_ctx,
           struct async_operation_ref *async_ref gcc_unused)
{
    Instance *ctx = (Instance *)_ctx;

    switch (ctx->mode) {
        Istream *body;
        static char data[0x100];

    case Instance::Mode::MODE_NULL:
        if (request->body != nullptr)
            sink_null_new(*request->pool, *request->body);

        http_server_response(request, HTTP_STATUS_NO_CONTENT,
                             HttpHeaders(), nullptr);
        break;

    case Instance::Mode::MIRROR:
        http_server_response(request,
                             request->body == nullptr
                             ? HTTP_STATUS_NO_CONTENT : HTTP_STATUS_OK,
                             HttpHeaders(),
                             request->body);
        break;

    case Instance::Mode::DUMMY:
        if (request->body != nullptr)
            sink_null_new(*request->pool, *request->body);

        body = istream_head_new(request->pool,
                                *istream_zero_new(request->pool),
                                256, false);
        body = istream_byte_new(*request->pool, *body);

        http_server_response(request, HTTP_STATUS_OK,
                             HttpHeaders(), body);
        break;

    case Instance::Mode::FIXED:
        if (request->body != nullptr)
            sink_null_new(*request->pool, *request->body);

        http_server_response(request, HTTP_STATUS_OK, HttpHeaders(),
                             istream_memory_new(request->pool, data, sizeof(data)));
        break;

    case Instance::Mode::HUGE_:
        if (request->body != nullptr)
            sink_null_new(*request->pool, *request->body);

        http_server_response(request, HTTP_STATUS_OK, HttpHeaders(),
                             istream_head_new(request->pool,
                                              *istream_zero_new(request->pool),
                                              512 * 1024, true));
        break;

    case Instance::Mode::HOLD:
        ctx->request_body = request->body != nullptr
            ? istream_hold_new(*request->pool, *request->body)
            : nullptr;

        body = istream_delayed_new(request->pool);
        ctx->operation.Init2<Instance>();
        istream_delayed_async_ref(*body)->Set(ctx->operation);

        http_server_response(request, HTTP_STATUS_OK, HttpHeaders(), body);

        static constexpr struct timeval t{0,0};
        ctx->timer.Add(t);
        break;
    }
}

static void
my_error(GError *error, void *_ctx)
{
    Instance *ctx = (Instance *)_ctx;

    ctx->timer.Cancel();
    ctx->shutdown_listener.Disable();

    g_printerr("%s\n", error->message);
    g_error_free(error);
}

static void
my_free(void *_ctx)
{
    Instance *ctx = (Instance *)_ctx;

    ctx->timer.Cancel();
    ctx->shutdown_listener.Disable();
}

static constexpr HttpServerConnectionHandler handler = {
    .request = my_request,
    .log = nullptr,
    .error = my_error,
    .free = my_free,
};

/*
 * main
 *
 */

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s INFD OUTFD {null|mirror|dummy|fixed|huge|hold}\n", argv[0]);
        return EXIT_FAILURE;
    }

    int in_fd, out_fd;

    if (strcmp(argv[1], "accept") == 0) {
        const int listen_fd = atoi(argv[2]);
        in_fd = out_fd = accept(listen_fd, nullptr, 0);
        if (in_fd < 0) {
            perror("accept() failed");
            return EXIT_FAILURE;
        }
    } else {
        in_fd = atoi(argv[1]);
        out_fd = atoi(argv[2]);
    }

    direct_global_init();
    Instance instance;
    fb_pool_init(false);
    instance.shutdown_listener.Enable();
    instance.timer.Init(timer_callback, &instance);

    RootPool pool;

    int sockfd;
    if (in_fd != out_fd) {
        sockfd = duplex_new(pool, in_fd, out_fd);
        if (sockfd < 0) {
            perror("duplex_new() failed");
            exit(2);
        }
    } else
        sockfd = in_fd;

    const char *mode = argv[3];
    if (strcmp(mode, "null") == 0)
        instance.mode = Instance::Mode::MODE_NULL;
    else if (strcmp(mode, "mirror") == 0)
        instance.mode = Instance::Mode::MIRROR;
    else if (strcmp(mode, "dummy") == 0)
        instance.mode = Instance::Mode::DUMMY;
    else if (strcmp(mode, "fixed") == 0)
        instance.mode = Instance::Mode::FIXED;
    else if (strcmp(mode, "huge") == 0)
        instance.mode = Instance::Mode::HUGE_;
    else if (strcmp(mode, "hold") == 0)
        instance.mode = Instance::Mode::HOLD;
    else {
        fprintf(stderr, "Unknown mode: %s\n", mode);
        return EXIT_FAILURE;
    }

    http_server_connection_new(pool, sockfd, FdType::FD_SOCKET,
                               nullptr, nullptr,
                               nullptr, nullptr,
                               true, &handler, &instance,
                               &instance.connection);

    instance.event_base.Dispatch();

    fb_pool_deinit();

    return EXIT_SUCCESS;
}
