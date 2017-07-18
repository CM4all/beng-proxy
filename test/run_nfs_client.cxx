#include "nfs/Handler.hxx"
#include "nfs/Client.hxx"
#include "nfs/Istream.hxx"
#include "istream/istream_pipe.hxx"
#include "istream/istream.hxx"
#include "istream/sink_fd.hxx"
#include "event/ShutdownListener.hxx"
#include "system/SetupProcess.hxx"
#include "io/FileDescriptor.hxx"
#include "PInstance.hxx"
#include "pool.hxx"
#include "http_response.hxx"
#include "direct.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

struct Context final : PInstance, NfsClientHandler, NfsClientOpenFileHandler {
    struct pool *pool;

    const char *path;

    ShutdownListener shutdown_listener;
    CancellablePointer cancel_ptr;

    NfsClient *client;

    bool aborted = false, failed = false, connected = false, closed = false;

    SinkFd *body;
    bool body_eof = false, body_abort = false, body_closed = false;

    Context()
        :shutdown_listener(event_loop, BIND_THIS_METHOD(ShutdownCallback)) {}

    void ShutdownCallback();

    /* virtual methods from NfsClientHandler */
    void OnNfsClientReady(NfsClient &client) override;
    void OnNfsMountError(std::exception_ptr ep) override;
    void OnNfsClientClosed(std::exception_ptr ep) override;

    /* virtual methods from class NfsClientOpenFileHandler */
    void OnNfsOpen(NfsFileHandle *handle, const struct stat *st) override;
    void OnNfsOpenError(std::exception_ptr ep) override;
};

void
Context::ShutdownCallback()
{
    aborted = true;

    if (body != nullptr)
        sink_fd_close(body);
    else
        cancel_ptr.Cancel();
}

/*
 * sink_fd handler
 *
 */

static void
my_sink_fd_input_eof(void *ctx)
{
    Context *c = (Context *)ctx;

    c->body = nullptr;
    c->body_eof = true;

    c->shutdown_listener.Disable();
    nfs_client_free(c->client);
}

static void
my_sink_fd_input_error(std::exception_ptr ep, void *ctx)
{
    Context *c = (Context *)ctx;

    PrintException(ep);

    c->body = nullptr;
    c->body_abort = true;

    c->shutdown_listener.Disable();
    nfs_client_free(c->client);
}

static bool
my_sink_fd_send_error(int error, void *ctx)
{
    Context *c = (Context *)ctx;

    fprintf(stderr, "%s\n", strerror(error));

    sink_fd_close(c->body);

    c->body = nullptr;
    c->body_abort = true;

    c->shutdown_listener.Disable();
    nfs_client_free(c->client);
    return false;
}

static constexpr SinkFdHandler my_sink_fd_handler = {
    .input_eof = my_sink_fd_input_eof,
    .input_error = my_sink_fd_input_error,
    .send_error = my_sink_fd_send_error,
};

/*
 * NfsClientOpenFileHandler
 *
 */

void
Context::OnNfsOpen(NfsFileHandle *handle, const struct stat *st)
{
    assert(!aborted);
    assert(!failed);
    assert(connected);

    auto *_body = istream_nfs_new(*pool, *handle, 0, st->st_size);
    _body = istream_pipe_new(pool, *_body, nullptr);
    body = sink_fd_new(event_loop, *pool, *_body,
                       FileDescriptor(STDOUT_FILENO),
                       guess_fd_type(STDOUT_FILENO),
                       my_sink_fd_handler, this);
    _body->Read();
}

void
Context::OnNfsOpenError(std::exception_ptr ep)
{
    assert(!aborted);
    assert(!failed);
    assert(connected);

    failed = true;

    PrintException(ep);

    shutdown_listener.Disable();
    nfs_client_free(client);
}

/*
 * nfs_client_handler
 *
 */

void
Context::OnNfsClientReady(NfsClient &_client)
{
    assert(!aborted);
    assert(!failed);
    assert(!connected);
    assert(!closed);

    connected = true;
    client = &_client;

    nfs_client_open_file(*client, *pool, path,
                         *this, cancel_ptr);
}

void
Context::OnNfsMountError(std::exception_ptr ep)
{
    assert(!aborted);
    assert(!failed);
    assert(!connected);
    assert(!closed);

    failed = true;

    PrintException(ep);

    shutdown_listener.Disable();
}

void
Context::OnNfsClientClosed(std::exception_ptr ep)
{
    assert(!aborted);
    assert(!failed);
    assert(connected);
    assert(!closed);

    closed = true;

    PrintException(ep);
}

/*
 * main
 *
 */

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: run_nfs_client SERVER ROOT PATH\n");
        return EXIT_FAILURE;
    }

    const char *const server = argv[1];
    const char *const _export = argv[2];

    Context ctx;
    ctx.path = argv[3];

    /* initialize */

    SetupProcess();

    direct_global_init();

    ctx.shutdown_listener.Enable();

    ctx.pool = pool_new_libc(ctx.root_pool, "pool");

    /* open NFS connection */

    nfs_client_new(ctx.event_loop, *ctx.pool, server, _export,
                   ctx, ctx.cancel_ptr);
    pool_unref(ctx.pool);

    /* run */

    ctx.event_loop.Dispatch();

    assert(ctx.aborted || ctx.failed || ctx.connected);

    /* cleanup */

    return ctx.connected
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
