#include "memcached-client.h"
#include "http-response.h"
#include "duplex.h"
#include "async.h"
#include "socket-util.h"
#include "growing-buffer.h"
#include "header-writer.h"
#include "lease.h"
#include "direct.h"
#include "istream-internal.h"

#include <glib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <event.h>

static int
connect_fake_server(void)
{
    int ret, sv[2];
    pid_t pid;

    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    if (ret < 0) {
        perror("socketpair() failed");
        exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid < 0) {
        perror("fork() failed");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        dup2(sv[1], 0);
        dup2(sv[1], 1);
        close(sv[0]);
        close(sv[1]);
        execl("./test/fake-memcached-server", "fake-memcached-server", NULL);
        perror("exec() failed");
        exit(EXIT_FAILURE);
    }

    close(sv[1]);

    socket_set_nonblock(sv[0], true);

    return sv[0];
}

struct context {
    pool_t pool;

    unsigned data_blocking;
    bool close_value_early, close_value_late, close_value_data;
    struct async_operation_ref async_ref;
    int fd;
    bool released, reuse;
    enum memcached_response_status status;

    istream_t delayed;

    istream_t value;
    off_t value_data, consumed_value_data;
    bool value_eof, value_abort;

    struct istream request_value;
};


/*
 * lease
 *
 */

static void
my_release(bool reuse, void *ctx)
{
    struct context *c = ctx;

    close(c->fd);
    c->fd = -1;
    c->released = true;
    c->reuse = reuse;
}

static const struct lease my_lease = {
    .release = my_release,
};


/*
 * request value istream
 *
 */

static const char request_value[8192];

struct request_value {
    struct istream base;

    struct async_operation_ref async_ref;

    bool read_close, read_abort;

    size_t sent;
};

static inline struct request_value *
istream_to_value(istream_t istream)
{
    return (struct request_value *)(((char*)istream) - offsetof(struct request_value, base));
}

static off_t
istream_request_value_available(istream_t istream, G_GNUC_UNUSED bool partial)
{
    const struct request_value *v = istream_to_value(istream);

    return sizeof(request_value) - v->sent;
}

static void
istream_request_value_read(istream_t istream)
{
    struct request_value *v = istream_to_value(istream);

    if (v->read_close)
        istream_deinit_abort(&v->base);
    else if (v->read_abort)
        async_abort(&v->async_ref);
    else if (v->sent >= sizeof(request_value))
        istream_deinit_eof(&v->base);
    else {
        size_t nbytes =
            istream_invoke_data(&v->base, request_value + v->sent,
                                sizeof(request_value) - v->sent);
        if (nbytes == 0)
            return;

        v->sent += nbytes;

        if (v->sent >= sizeof(request_value))
            istream_deinit_eof(&v->base);
    }
}

static void
istream_request_value_close(istream_t istream)
{
    struct request_value *v = istream_to_value(istream);

    istream_deinit_abort(&v->base);
}

static const struct istream istream_request_value = {
    .available = istream_request_value_available,
    .read = istream_request_value_read,
    .close = istream_request_value_close,
};

static istream_t
request_value_new(pool_t pool, bool read_close, bool read_abort)
{
    struct request_value *v = (struct request_value *)
        istream_new(pool, &istream_request_value, sizeof(*v));

    v->read_close = read_close;
    v->read_abort = read_abort;
    v->sent = 0;

    return istream_struct_cast(&v->base);
}

static struct async_operation_ref *
request_value_async_ref(istream_t istream)
{
    struct request_value *v = istream_to_value(istream);

    return &v->async_ref;
}

/*
 * istream handler
 *
 */

static size_t
my_istream_data(G_GNUC_UNUSED const void *data, size_t length, void *ctx)
{
    struct context *c = ctx;

    c->value_data += length;

    if (c->close_value_data) {
        istream_close(c->value);
        return 0;
    }

    if (c->data_blocking) {
        --c->data_blocking;
        return 0;
    }

    c->consumed_value_data += length;
    return length;
}

static void
my_istream_eof(void *ctx)
{
    struct context *c = ctx;

    c->value = NULL;
    c->value_eof = true;
}

static void
my_istream_abort(void *ctx)
{
    struct context *c = ctx;

    c->value = NULL;
    c->value_abort = true;
}

static const struct istream_handler my_istream_handler = {
    .data = my_istream_data,
    .eof = my_istream_eof,
    .abort = my_istream_abort,
};


/*
 * memcached_response_handler_t
 *
 */

static void
on_memcached_response(enum memcached_response_status status,
                      G_GNUC_UNUSED const void *extras,
                      G_GNUC_UNUSED size_t extras_length,
                      G_GNUC_UNUSED const void *key,
                      G_GNUC_UNUSED size_t key_length,
                      istream_t value, void *ctx)
{
    struct context *c = ctx;

    c->status = status;

    if (c->close_value_early)
        istream_close(value);
    else if (value != NULL)
        istream_assign_handler(&c->value, value, &my_istream_handler, c, 0);

    if (c->close_value_late)
        istream_close(c->value);
}


/*
 * tests
 *
 */

static void
test_basic(pool_t pool, struct context *c)
{
    c->fd = connect_fake_server();

    memcached_client_invoke(pool, c->fd, &my_lease, c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            NULL,
                            on_memcached_response, c,
                            &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->reuse);
    assert(c->fd < 0);
    assert(c->status == MEMCACHED_STATUS_NO_ERROR);
    assert(c->value == NULL);
    assert(c->value_eof);
    assert(!c->value_abort);
}

static void
test_close_early(pool_t pool, struct context *c)
{
    c->fd = connect_fake_server();
    c->close_value_early = true;

    memcached_client_invoke(pool, c->fd, &my_lease, c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            NULL,
                            on_memcached_response, c,
                            &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(!c->reuse);
    assert(c->fd < 0);
    assert(c->status == MEMCACHED_STATUS_NO_ERROR);
    assert(c->value == NULL);
    assert(!c->value_eof);
    assert(!c->value_abort);
    assert(c->value_data == 0);
}

static void
test_close_late(pool_t pool, struct context *c)
{
    c->fd = connect_fake_server();
    c->close_value_late = true;

    memcached_client_invoke(pool, c->fd, &my_lease, c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            NULL,
                            on_memcached_response, c,
                            &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(!c->reuse);
    assert(c->fd < 0);
    assert(c->status == MEMCACHED_STATUS_NO_ERROR);
    assert(c->value == NULL);
    assert(!c->value_eof);
    assert(c->value_abort);
    assert(c->value_data == 0);
}

static void
test_close_data(pool_t pool, struct context *c)
{
    c->fd = connect_fake_server();
    c->close_value_data = true;

    memcached_client_invoke(pool, c->fd, &my_lease, c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            NULL,
                            on_memcached_response, c,
                            &c->async_ref);
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(!c->reuse);
    assert(c->fd < 0);
    assert(c->status == MEMCACHED_STATUS_NO_ERROR);
    assert(c->value == NULL);
    assert(!c->value_eof);
    assert(c->value_abort);
    assert(c->value_data > 0);
}

static void
test_abort(pool_t pool, struct context *c)
{
    c->fd = connect_fake_server();
    c->close_value_data = true;

    memcached_client_invoke(pool, c->fd, &my_lease, c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            NULL,
                            on_memcached_response, c,
                            &c->async_ref);
    pool_unref(pool);
    pool_commit();

    async_abort(&c->async_ref);

    assert(c->released);
    assert(!c->reuse);
    assert(c->fd < 0);
    assert(c->value == NULL);
    assert(!c->value_eof);
    assert(!c->value_abort);
}

static void
test_request_value(pool_t pool, struct context *c)
{
    istream_t value;

    c->fd = connect_fake_server();

    value = request_value_new(c->pool, false, false);

    memcached_client_invoke(pool, c->fd, &my_lease, c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            value,
                            on_memcached_response, c,
                            request_value_async_ref(value));
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(c->reuse);
    assert(c->fd < 0);
    assert(c->status == MEMCACHED_STATUS_NO_ERROR);
    assert(c->value == NULL);
    assert(c->value_eof);
    assert(!c->value_abort);
}

static void
test_request_value_close(pool_t pool, struct context *c)
{
    istream_t value;

    c->fd = connect_fake_server();

    value = request_value_new(c->pool, true, false);

    memcached_client_invoke(pool, c->fd, &my_lease, c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            value,
                            on_memcached_response, c,
                            request_value_async_ref(value));
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(!c->reuse);
    assert(c->fd < 0);
}

static void
test_request_value_abort(pool_t pool, struct context *c)
{
    istream_t value;

    c->fd = connect_fake_server();

    value = request_value_new(c->pool, false, true);

    memcached_client_invoke(pool, c->fd, &my_lease, c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            value,
                            on_memcached_response, c,
                            request_value_async_ref(value));
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(!c->reuse);
    assert(c->fd < 0);
}

/*
 * main
 *
 */

static void
run_test(pool_t pool, void (*test)(pool_t pool, struct context *c)) {
    struct context c;

    memset(&c, 0, sizeof(c));
    c.pool = pool_new_linear(pool, "test", 16384);
    test(c.pool, &c);
    pool_commit();
}

int main(int argc, char **argv) {
    struct event_base *event_base;
    pool_t pool;

    (void)argc;
    (void)argv;

    signal(SIGPIPE, SIG_IGN);

    direct_global_init();
    event_base = event_init();

    pool = pool_new_libc(NULL, "root");

    run_test(pool, test_basic);
    run_test(pool, test_close_early);
    run_test(pool, test_close_late);
    run_test(pool, test_close_data);
    run_test(pool, test_abort);
    run_test(pool, test_request_value);
    run_test(pool, test_request_value_close);
    run_test(pool, test_request_value_abort);

    pool_unref(pool);
    pool_commit();
    pool_recycler_clear();

    event_base_free(event_base);
    direct_global_deinit();
}
