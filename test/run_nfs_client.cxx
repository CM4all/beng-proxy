#include "nfs_client.hxx"
#include "istream_nfs.hxx"
#include "istream/istream_pipe.hxx"
#include "istream/istream.hxx"
#include "istream/sink_fd.hxx"
#include "event/ShutdownListener.hxx"
#include "async.hxx"
#include "pool.hxx"
#include "http_response.hxx"
#include "direct.hxx"

#include <stdio.h>
#include <stdlib.h>

struct Context final : NfsClientHandler {
    struct pool *pool;

    const char *path;

    ShutdownListener shutdown_listener;
    struct async_operation_ref async_ref;

    NfsClient *client;

    bool aborted, failed, connected, closed;

    SinkFd *body;
    bool body_eof, body_abort, body_closed;

    Context()
        :shutdown_listener(ShutdownCallback, this) {}

    static void ShutdownCallback(void *ctx);

    /* virtual methods from NfsClientHandler */
    void OnNfsClientReady(NfsClient &client) override;
    void OnNfsMountError(GError *error) override;
    void OnNfsClientClosed(GError *error) override;
};

void
Context::ShutdownCallback(void *ctx)
{
    Context *c = (Context *)ctx;

    c->aborted = true;

    if (c->body != nullptr)
        sink_fd_close(c->body);
    else
        c->async_ref.Abort();
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
my_sink_fd_input_error(GError *error, void *ctx)
{
    Context *c = (Context *)ctx;

    g_printerr("%s\n", error->message);
    g_error_free(error);

    c->body = nullptr;
    c->body_abort = true;

    c->shutdown_listener.Disable();
    nfs_client_free(c->client);
}

static bool
my_sink_fd_send_error(int error, void *ctx)
{
    Context *c = (Context *)ctx;

    g_printerr("%s\n", g_strerror(error));

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

static void
my_open_ready(NfsFileHandle *handle, const struct stat *st, void *ctx)
{
    Context *c = (Context *)ctx;

    assert(!c->aborted);
    assert(!c->failed);
    assert(c->connected);

    Istream *body = istream_nfs_new(*c->pool, *handle, 0, st->st_size);
    body = istream_pipe_new(c->pool, *body, nullptr);
    c->body = sink_fd_new(*c->pool, *body, 1, guess_fd_type(1),
                          my_sink_fd_handler, ctx);
    body->Read();
}

static void
my_open_error(GError *error, void *ctx)
{
    Context *c = (Context *)ctx;

    assert(!c->aborted);
    assert(!c->failed);
    assert(c->connected);

    c->failed = true;

    g_printerr("open error: %s\n", error->message);
    g_error_free(error);

    c->shutdown_listener.Disable();
    nfs_client_free(c->client);
}

static constexpr NfsClientOpenFileHandler my_open_handler = {
    .ready = my_open_ready,
    .error = my_open_error,
};

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

    nfs_client_open_file(client, pool, path,
                         &my_open_handler, this,
                         &async_ref);
}

void
Context::OnNfsMountError(GError *error)
{
    assert(!aborted);
    assert(!failed);
    assert(!connected);
    assert(!closed);

    failed = true;

    g_printerr("mount error: %s\n", error->message);
    g_error_free(error);

    shutdown_listener.Disable();
}

void
Context::OnNfsClientClosed(GError *error)
{
    assert(!aborted);
    assert(!failed);
    assert(connected);
    assert(!closed);

    closed = true;

    g_printerr("closed: %s\n", error->message);
    g_error_free(error);
}

/*
 * main
 *
 */

int main(int argc, char **argv) {
    struct event_base *event_base;
    static Context ctx;

    if (argc != 4) {
        g_printerr("usage: run_nfs_client SERVER ROOT PATH\n");
        return EXIT_FAILURE;
    }

    const char *const server = argv[1];
    const char *const _export = argv[2];
    ctx.path = argv[3];

    /* initialize */

    signal(SIGPIPE, SIG_IGN);

    direct_global_init();

    event_base = event_init();
    ctx.shutdown_listener.Enable();

    struct pool *const root_pool = pool_new_libc(nullptr, "root");
    ctx.pool = pool_new_libc(root_pool, "pool");

    /* open NFS connection */

    nfs_client_new(ctx.pool, server, _export,
                   ctx, &ctx.async_ref);
    pool_unref(ctx.pool);

    /* run */

    event_dispatch();

    assert(ctx.aborted || ctx.failed || ctx.connected);

    /* cleanup */

    pool_commit();

    pool_unref(root_pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
    direct_global_deinit();

    return ctx.connected
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
