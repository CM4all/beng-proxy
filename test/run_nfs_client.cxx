#include "nfs_client.hxx"
#include "istream_nfs.hxx"
#include "shutdown_listener.h"
#include "async.hxx"
#include "pool.hxx"
#include "http_response.hxx"
#include "istream.h"
#include "sink_fd.h"
#include "direct.h"

#include <stdio.h>
#include <stdlib.h>

struct Context {
    struct pool *pool;

    const char *path;

    struct shutdown_listener shutdown_listener;
    struct async_operation_ref async_ref;

    struct nfs_client *client;

    bool aborted, failed, connected, closed;

    struct sink_fd *body;
    bool body_eof, body_abort, body_closed;
};

static void
shutdown_callback(void *ctx)
{
    Context *c = (Context *)ctx;

    c->aborted = true;

    if (c->body != nullptr)
        sink_fd_close(c->body);
    else
        async_abort(&c->async_ref);
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

    shutdown_listener_deinit(&c->shutdown_listener);
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

    shutdown_listener_deinit(&c->shutdown_listener);
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

    shutdown_listener_deinit(&c->shutdown_listener);
    nfs_client_free(c->client);
    return false;
}

static const struct sink_fd_handler my_sink_fd_handler = {
    .input_eof = my_sink_fd_input_eof,
    .input_error = my_sink_fd_input_error,
    .send_error = my_sink_fd_send_error,
};

/*
 * nfs_client_open_file_handler
 *
 */

static void
my_open_ready(struct nfs_file_handle *handle, const struct stat *st, void *ctx)
{
    Context *c = (Context *)ctx;

    assert(!c->aborted);
    assert(!c->failed);
    assert(c->connected);

    struct istream *body = istream_nfs_new(c->pool, handle, 0, st->st_size);
    body = istream_pipe_new(c->pool, body, nullptr);
    c->body = sink_fd_new(c->pool, body, 1, guess_fd_type(1),
                          &my_sink_fd_handler, ctx);
    istream_read(body);
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

    shutdown_listener_deinit(&c->shutdown_listener);
    nfs_client_free(c->client);
}

static const struct nfs_client_open_file_handler my_open_handler = {
    .ready = my_open_ready,
    .error = my_open_error,
};

/*
 * nfs_client_handler
 *
 */

static void
my_nfs_client_ready(struct nfs_client *client, void *ctx)
{
    Context *c = (Context *)ctx;

    assert(!c->aborted);
    assert(!c->failed);
    assert(!c->connected);
    assert(!c->closed);

    c->connected = true;
    c->client = client;

    nfs_client_open_file(client, c->pool, c->path,
                         &my_open_handler, ctx,
                         &c->async_ref);
}

static void
my_nfs_client_mount_error(GError *error, void *ctx)
{
    Context *c = (Context *)ctx;

    assert(!c->aborted);
    assert(!c->failed);
    assert(!c->connected);
    assert(!c->closed);

    c->failed = true;

    g_printerr("mount error: %s\n", error->message);
    g_error_free(error);

    shutdown_listener_deinit(&c->shutdown_listener);
}

static void
my_nfs_client_closed(GError *error, void *ctx)
{
    Context *c = (Context *)ctx;

    assert(!c->aborted);
    assert(!c->failed);
    assert(c->connected);
    assert(!c->closed);

    c->closed = true;

    g_printerr("closed: %s\n", error->message);
    g_error_free(error);
}

static const struct nfs_client_handler my_nfs_client_handler = {
    .ready = my_nfs_client_ready,
    .mount_error = my_nfs_client_mount_error,
    .closed = my_nfs_client_closed,
};

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
    shutdown_listener_init(&ctx.shutdown_listener, shutdown_callback, &ctx);

    struct pool *const root_pool = pool_new_libc(nullptr, "root");
    ctx.pool = pool_new_libc(root_pool, "pool");

    /* open NFS connection */

    nfs_client_new(ctx.pool, server, _export,
                   &my_nfs_client_handler, &ctx,
                   &ctx.async_ref);
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
