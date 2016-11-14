#include "memcached/memcached_client.hxx"
#include "http_response.hxx"
#include "system/SetupProcess.hxx"
#include "system/fd-util.h"
#include "system/fd_util.h"
#include "header_writer.hxx"
#include "lease.hxx"
#include "direct.hxx"
#include "istream/Pointer.hxx"
#include "istream/istream.hxx"
#include "fb_pool.hxx"
#include "pool.hxx"
#include "RootPool.hxx"
#include "event/Loop.hxx"
#include "util/Cast.hxx"
#include "util/Cancellable.hxx"

#include <glib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

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
        execl("./test/fake_memcached_server", "fake_memcached_server",
              nullptr);
        perror("exec() failed");
        exit(EXIT_FAILURE);
    }

    close(sv[1]);

    fd_set_nonblock(sv[0], true);

    return sv[0];
}

struct Context final : Lease, IstreamHandler {
    EventLoop event_loop;

    struct pool *pool;

    unsigned data_blocking = 0;
    bool close_value_early = false;
    bool close_value_late = false;
    bool close_value_data = false;
    CancellablePointer cancel_ptr;
    int fd = -1;
    bool released = false, reuse = false, got_response = false;
    enum memcached_response_status status;

    IstreamPointer value;
    off_t value_data = 0, consumed_value_data = 0;
    bool value_eof = false, value_abort = false, value_closed = false;

    Context():value(nullptr) {}

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) override;
    void OnEof() override;
    void OnError(GError *error) override;

    /* virtual methods from class Lease */
    void ReleaseLease(bool _reuse) override {
        close(fd);
        fd = -1;
        released = true;
        reuse = _reuse;
    }
};

/*
 * request value istream
 *
 */

static char request_value[8192];

struct RequestValueIstream final : public Istream {
    CancellablePointer cancel_ptr;

    bool read_close, read_abort;

    size_t sent = 0;

    RequestValueIstream(struct pool &p, bool _read_close, bool _read_abort)
        :Istream(p), read_close(_read_close), read_abort(_read_abort) {}

    off_t _GetAvailable(gcc_unused bool partial) override {
        return sizeof(request_value) - sent;
    }

    void _Read() override;
};

void
RequestValueIstream::_Read()
{
    if (read_close) {
        GError *error = g_error_new_literal(test_quark(), 0, "read_close");
        DestroyError(error);
    } else if (read_abort)
        cancel_ptr.Cancel();
    else if (sent >= sizeof(request_value))
        DestroyEof();
    else {
        size_t nbytes = InvokeData(request_value + sent,
                                   sizeof(request_value) - sent);
        if (nbytes == 0)
            return;

        sent += nbytes;

        if (sent >= sizeof(request_value))
            DestroyEof();
    }
}

static Istream *
request_value_new(struct pool *pool, bool read_close, bool read_abort)
{
    return NewIstream<RequestValueIstream>(*pool, read_close, read_abort);
}

static CancellablePointer &
request_value_cancel_ptr(Istream &istream)
{
    auto &v = (RequestValueIstream &)istream;

    return v.cancel_ptr;
}

/*
 * istream handler
 *
 */

size_t
Context::OnData(gcc_unused const void *data, size_t length)
{
    value_data += length;

    if (close_value_data) {
        value_closed = true;
        value.ClearAndClose();
        return 0;
    }

    if (data_blocking) {
        --data_blocking;
        return 0;
    }

    consumed_value_data += length;
    return length;
}

void
Context::OnEof()
{
    value.Clear();
    value_eof = true;
}

void
Context::OnError(GError *error)
{
    g_error_free(error);

    value.Clear();
    value_abort = true;
}

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
                Istream *value, void *ctx)
{
    auto *c = (Context *)ctx;

    assert(!c->got_response);

    c->got_response = true;
    c->status = status;

    if (c->close_value_early)
        value->CloseUnused();
    else if (value != NULL)
        c->value.Set(*value, *c);

    if (c->close_value_late) {
        c->value_closed = true;
        c->value.ClearAndClose();
    }
}

static void
my_mcd_error(GError *error, gcc_unused void *ctx)
{
    auto *c = (Context *)ctx;

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
test_basic(struct pool *pool, Context *c)
{
    c->fd = connect_fake_server();

    memcached_client_invoke(pool, c->event_loop,
                            c->fd, FdType::FD_SOCKET, *c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            NULL,
                            &my_mcd_handler, c,
                            c->cancel_ptr);
    pool_unref(pool);
    pool_commit();

    c->event_loop.Dispatch();

    assert(c->released);
    assert(c->reuse);
    assert(c->fd < 0);
    assert(c->status == MEMCACHED_STATUS_NO_ERROR);
    assert(!c->value.IsDefined());
    assert(c->value_eof);
    assert(!c->value_abort);
}

static void
test_close_early(struct pool *pool, Context *c)
{
    c->fd = connect_fake_server();
    c->close_value_early = true;

    memcached_client_invoke(pool, c->event_loop,
                            c->fd, FdType::FD_SOCKET, *c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            NULL,
                            &my_mcd_handler, c,
                            c->cancel_ptr);
    pool_unref(pool);
    pool_commit();

    c->event_loop.Dispatch();

    assert(c->released);
    assert(!c->reuse);
    assert(c->fd < 0);
    assert(c->status == MEMCACHED_STATUS_NO_ERROR);
    assert(!c->value.IsDefined());
    assert(!c->value_eof);
    assert(!c->value_abort);
    assert(c->value_data == 0);
}

static void
test_close_late(struct pool *pool, Context *c)
{
    c->fd = connect_fake_server();
    c->close_value_late = true;

    memcached_client_invoke(pool, c->event_loop,
                            c->fd, FdType::FD_SOCKET, *c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            NULL,
                            &my_mcd_handler, c,
                            c->cancel_ptr);
    pool_unref(pool);
    pool_commit();

    c->event_loop.Dispatch();

    assert(c->released);
    assert(!c->reuse);
    assert(c->fd < 0);
    assert(c->status == MEMCACHED_STATUS_NO_ERROR);
    assert(!c->value.IsDefined());
    assert(!c->value_eof);
    assert(!c->value_abort);
    assert(c->value_closed);
    assert(c->value_data == 0);
}

static void
test_close_data(struct pool *pool, Context *c)
{
    c->fd = connect_fake_server();
    c->close_value_data = true;

    memcached_client_invoke(pool, c->event_loop,
                            c->fd, FdType::FD_SOCKET, *c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            NULL,
                            &my_mcd_handler, c,
                            c->cancel_ptr);
    pool_unref(pool);
    pool_commit();

    c->event_loop.Dispatch();

    assert(c->released);
    assert(!c->reuse);
    assert(c->fd < 0);
    assert(c->status == MEMCACHED_STATUS_NO_ERROR);
    assert(!c->value.IsDefined());
    assert(!c->value_eof);
    assert(!c->value_abort);
    assert(c->value_closed);
    assert(c->value_data > 0);
}

static void
test_abort(struct pool *pool, Context *c)
{
    c->fd = connect_fake_server();
    c->close_value_data = true;

    memcached_client_invoke(pool, c->event_loop,
                            c->fd, FdType::FD_SOCKET, *c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            NULL,
                            &my_mcd_handler, c,
                            c->cancel_ptr);
    pool_unref(pool);
    pool_commit();

    c->cancel_ptr.Cancel();

    assert(!c->got_response);
    assert(c->released);
    assert(!c->reuse);
    assert(c->fd < 0);
    assert(!c->value.IsDefined());
    assert(!c->value_eof);
    assert(!c->value_abort);
}

static void
test_request_value(struct pool *pool, Context *c)
{
    Istream *value;

    c->fd = connect_fake_server();

    value = request_value_new(c->pool, false, false);

    memcached_client_invoke(pool, c->event_loop,
                            c->fd, FdType::FD_SOCKET, *c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            value,
                            &my_mcd_handler, c,
                            request_value_cancel_ptr(*value));
    pool_unref(pool);
    pool_commit();

    c->event_loop.Dispatch();

    assert(c->released);
    assert(c->reuse);
    assert(c->fd < 0);
    assert(c->status == MEMCACHED_STATUS_NO_ERROR);
    assert(!c->value.IsDefined());
    assert(c->value_eof);
    assert(!c->value_abort);
}

static void
test_request_value_close(struct pool *pool, Context *c)
{
    Istream *value;

    c->fd = connect_fake_server();

    value = request_value_new(c->pool, true, false);

    memcached_client_invoke(pool, c->event_loop,
                            c->fd, FdType::FD_SOCKET, *c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            value,
                            &my_mcd_handler, c,
                            request_value_cancel_ptr(*value));
    pool_unref(pool);
    pool_commit();

    c->event_loop.Dispatch();

    assert(c->released);
    assert(!c->reuse);
    assert(c->fd < 0);
}

static void
test_request_value_abort(struct pool *pool, Context *c)
{
    Istream *value;

    c->fd = connect_fake_server();

    value = request_value_new(c->pool, false, true);

    memcached_client_invoke(pool, c->event_loop,
                            c->fd, FdType::FD_SOCKET, *c,
                            MEMCACHED_OPCODE_SET,
                            NULL, 0,
                            "foo", 3,
                            value,
                            &my_mcd_handler, c,
                            request_value_cancel_ptr(*value));
    pool_unref(pool);
    pool_commit();

    c->event_loop.Dispatch();

    assert(c->released);
    assert(!c->reuse);
    assert(c->fd < 0);
}

/*
 * main
 *
 */

static void
run_test(struct pool *pool, void (*test)(struct pool *pool, Context *c))
{
    Context c;

    c.pool = pool_new_linear(pool, "test", 16384);
    test(c.pool, &c);
    pool_commit();
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    EventLoop event_loop;

    SetupProcess();

    direct_global_init();
    fb_pool_init();

    RootPool pool;

    run_test(pool, test_basic);
    run_test(pool, test_close_early);
    run_test(pool, test_close_late);
    run_test(pool, test_close_data);
    run_test(pool, test_abort);
    run_test(pool, test_request_value);
    run_test(pool, test_request_value_close);
    run_test(pool, test_request_value_abort);

    fb_pool_deinit();
}
