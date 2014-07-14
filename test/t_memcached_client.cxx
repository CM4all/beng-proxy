#include "memcached_client.hxx"
#include "http_response.hxx"
#include "duplex.h"
#include "async.hxx"
#include "fd-util.h"
#include "growing_buffer.hxx"
#include "header_writer.hxx"
#include "lease.h"
#include "direct.h"
#include "istream-internal.h"
#include "fd_util.h"
#include "fb_pool.h"

#include <glib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <event.h>

static inline GQuark
test_quark(void)
{
    return g_quark_from_static_string("test");
}

static int
connect_fake_server(void)
{
    int ret, sv[2];
    pid_t pid;

    ret = socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, sv);
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

    fd_set_nonblock(sv[0], true);

    return sv[0];
}

struct context {
    struct pool *pool;

    unsigned data_blocking;
    bool close_value_early, close_value_late, close_value_data;
    struct async_operation_ref async_ref;
    int fd;
    bool released, reuse, got_response;
    enum memcached_response_status status;

    struct istream *delayed;

    struct istream *value;
    off_t value_data, consumed_value_data;
    bool value_eof, value_abort, value_closed;

    struct istream request_value;
};


/*
 * lease
 *
 */

static void
my_release(bool reuse, void *ctx)
{
    struct context *c = (struct context *)ctx;

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

static char request_value[8192];

struct request_value {
    struct istream base;

    struct async_operation_ref async_ref;

    bool read_close, read_abort;

    size_t sent;
};

static inline struct request_value *
istream_to_value(struct istream *istream)
{
    void *p = ((char *)istream) - offsetof(struct request_value, base);
    return (struct request_value *)p;
}

static off_t
istream_request_value_available(struct istream *istream, gcc_unused bool partial)
{
    const struct request_value *v = istream_to_value(istream);

    return sizeof(request_value) - v->sent;
}

static void
istream_request_value_read(struct istream *istream)
{
    struct request_value *v = istream_to_value(istream);

    if (v->read_close) {
        GError *error = g_error_new_literal(test_quark(), 0, "read_close");
        istream_deinit_abort(&v->base, error);
    } else if (v->read_abort)
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
istream_request_value_close(struct istream *istream)
{
    struct request_value *v = istream_to_value(istream);

    istream_deinit(&v->base);
}

static const struct istream_class istream_request_value = {
    .available = istream_request_value_available,
    .read = istream_request_value_read,
    .close = istream_request_value_close,
};

static struct istream *
request_value_new(struct pool *pool, bool read_close, bool read_abort)
{
    struct request_value *v = (struct request_value *)
        istream_new(pool, &istream_request_value, sizeof(*v));

    v->read_close = read_close;
    v->read_abort = read_abort;
    v->sent = 0;

    return istream_struct_cast(&v->base);
}

static struct async_operation_ref *
request_value_async_ref(struct istream *istream)
{
    struct request_value *v = istream_to_value(istream);

    return &v->async_ref;
}

/*
 * istream handler
 *
 */

static size_t
my_istream_data(gcc_unused const void *data, size_t length, void *ctx)
{
    struct context *c = (struct context *)ctx;

    c->value_data += length;

    if (c->close_value_data) {
        c->value_closed = true;
        istream_free_handler(&c->value);
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
    struct context *c = (struct context *)ctx;

    c->value = NULL;
    c->value_eof = true;
}

static void
my_istream_abort(GError *error, void *ctx)
{
    struct context *c = (struct context *)ctx;

    g_error_free(error);

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
my_mcd_response(enum memcached_response_status status,
                gcc_unused const void *extras,
                gcc_unused size_t extras_length,
                gcc_unused const void *key,
                gcc_unused size_t key_length,
                struct istream *value, void *ctx)
{
    struct context *c = (struct context *)ctx;

    assert(!c->got_response);

    c->got_response = true;
    c->status = status;

    if (c->close_value_early)
        istream_close_unused(value);
    else if (value != NULL)
        istream_assign_handler(&c->value, value, &my_istream_handler, c, 0);

    if (c->close_value_late) {
        c->value_closed = true;
        istream_free_handler(&c->value);
    }
}

static void
my_mcd_error(GError *error, gcc_unused void *ctx)
{
    struct context *c = (struct context *)ctx;

    assert(!c->got_response);

    g_error_free(error);

    c->got_response = true;
    c->status = (memcached_response_status)-1;
}

static const struct memcached_client_handler my_mcd_handler = {
    .response = my_mcd_response,
    .error = my_mcd_error,
};


/*
 * tests
 *
 */

static void
test_basic(struct pool *pool, struct context *c)
{
    c->fd = connect_fake_server();

    memcached_client_invoke(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            NULL,
                            &my_mcd_handler, c,
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
test_close_early(struct pool *pool, struct context *c)
{
    c->fd = connect_fake_server();
    c->close_value_early = true;

    memcached_client_invoke(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            NULL,
                            &my_mcd_handler, c,
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
test_close_late(struct pool *pool, struct context *c)
{
    c->fd = connect_fake_server();
    c->close_value_late = true;

    memcached_client_invoke(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            NULL,
                            &my_mcd_handler, c,
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
    assert(c->value_closed);
    assert(c->value_data == 0);
}

static void
test_close_data(struct pool *pool, struct context *c)
{
    c->fd = connect_fake_server();
    c->close_value_data = true;

    memcached_client_invoke(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            NULL,
                            &my_mcd_handler, c,
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
    assert(c->value_closed);
    assert(c->value_data > 0);
}

static void
test_abort(struct pool *pool, struct context *c)
{
    c->fd = connect_fake_server();
    c->close_value_data = true;

    memcached_client_invoke(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            NULL,
                            &my_mcd_handler, c,
                            &c->async_ref);
    pool_unref(pool);
    pool_commit();

    async_abort(&c->async_ref);

    assert(!c->got_response);
    assert(c->released);
    assert(!c->reuse);
    assert(c->fd < 0);
    assert(c->value == NULL);
    assert(!c->value_eof);
    assert(!c->value_abort);
}

static void
test_request_value(struct pool *pool, struct context *c)
{
    struct istream *value;

    c->fd = connect_fake_server();

    value = request_value_new(c->pool, false, false);

    memcached_client_invoke(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            value,
                            &my_mcd_handler, c,
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
test_request_value_close(struct pool *pool, struct context *c)
{
    struct istream *value;

    c->fd = connect_fake_server();

    value = request_value_new(c->pool, true, false);

    memcached_client_invoke(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            value,
                            &my_mcd_handler, c,
                            request_value_async_ref(value));
    pool_unref(pool);
    pool_commit();

    event_dispatch();

    assert(c->released);
    assert(!c->reuse);
    assert(c->fd < 0);
}

static void
test_request_value_abort(struct pool *pool, struct context *c)
{
    struct istream *value;

    c->fd = connect_fake_server();

    value = request_value_new(c->pool, false, true);

    memcached_client_invoke(pool, c->fd, ISTREAM_SOCKET, &my_lease, c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            value,
                            &my_mcd_handler, c,
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
run_test(struct pool *pool, void (*test)(struct pool *pool, struct context *c)) {
    struct context c;

    memset(&c, 0, sizeof(c));
    c.pool = pool_new_linear(pool, "test", 16384);
    test(c.pool, &c);
    pool_commit();
}

int main(int argc, char **argv) {
    struct event_base *event_base;
    struct pool *pool;

    (void)argc;
    (void)argv;

    signal(SIGPIPE, SIG_IGN);

    direct_global_init();
    event_base = event_init();
    fb_pool_init(false);

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

    fb_pool_deinit();
    event_base_free(event_base);
    direct_global_deinit();
}
