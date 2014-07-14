/*
 * Monitor which expects a string on a TCP connection.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_expect_monitor.hxx"
#include "lb_monitor.hxx"
#include "lb_config.hxx"
#include "pool.hxx"
#include "async.hxx"
#include "gerrno.h"
#include "net/ConnectSocket.hxx"
#include "net/SocketDescriptor.hxx"
#include "net/SocketAddress.hxx"
#include "util/Cast.hxx"

#include <event.h>
#include <unistd.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>

struct ExpectMonitor {
    struct pool *pool;
    const lb_monitor_config *config;

    int fd;

    struct event event;

    LBMonitorHandler *handler;

    struct async_operation_ref *async_ref;
    struct async_operation async_operation;

    ExpectMonitor(struct pool *_pool, const lb_monitor_config *_config,
                  LBMonitorHandler &_handler,
                  async_operation_ref *_async_ref)
        :pool(_pool), config(_config),
         handler(&_handler),
         async_ref(_async_ref) {}

    ExpectMonitor(const ExpectMonitor &other) = delete;
};

static bool
check_expectation(char *received, size_t received_length,
                  const char *expect)
{
    return g_strrstr_len(received, received_length, expect) != NULL;
}

/*
 * async operation
 *
 */

static void
expect_monitor_request_abort(struct async_operation *ao)
{
    ExpectMonitor *expect =
        &ContainerCast2(*ao, &ExpectMonitor::async_operation);

    event_del(&expect->event);
    close(expect->fd);
    pool_unref(expect->pool);
    delete expect;
}

static const struct async_operation_class expect_monitor_async_operation = {
    .abort = expect_monitor_request_abort,
};

/*
 * libevent callback
 *
 */

static void
expect_monitor_event_callback(gcc_unused int fd, short event, void *ctx)
{
    ExpectMonitor *expect =
        (ExpectMonitor *)ctx;

    expect->async_operation.Finished();

    if (event & EV_TIMEOUT) {
        close(expect->fd);
        expect->handler->Timeout();
    } else {
        char buffer[1024];

        ssize_t nbytes = recv(expect->fd, buffer, sizeof(buffer),
                              MSG_DONTWAIT);
        if (nbytes < 0) {
            GError *error = new_error_errno();
            close(fd);
            expect->handler->Error(error);
        } else if (!expect->config->fade_expect.empty() &&
                   check_expectation(buffer, nbytes,
                                     expect->config->fade_expect.c_str())) {
            close(fd);
            expect->handler->Fade();
        } else if (expect->config->expect.empty() ||
                   check_expectation(buffer, nbytes,
                                     expect->config->expect.c_str())) {
            close(fd);
            expect->handler->Success();
        } else {
            close(fd);
            GError *error = g_error_new_literal(g_file_error_quark(), 0,
                                                "Expectation failed");
            expect->handler->Error(error);
        }
    }

    pool_unref(expect->pool);
    delete expect;
    pool_commit();
}

/*
 * client_socket handler
 *
 */

static void
expect_monitor_success(SocketDescriptor &&fd, void *ctx)
{
    ExpectMonitor *expect =
        (ExpectMonitor *)ctx;

    if (!expect->config->send.empty()) {
        ssize_t nbytes = send(fd.Get(), expect->config->send.data(),
                              expect->config->send.length(),
                              MSG_DONTWAIT);
        if (nbytes < 0) {
            GError *error = new_error_errno();
            expect->handler->Error(error);
            return;
        }
    }

    struct timeval expect_timeout = {
        time_t(expect->config->timeout > 0 ? expect->config->timeout : 10),
        0,
    };

    expect->fd = fd.Steal();
    event_set(&expect->event, expect->fd, EV_READ|EV_TIMEOUT,
              expect_monitor_event_callback, expect);
    event_add(&expect->event, &expect_timeout);

    expect->async_operation.Init(expect_monitor_async_operation);
    expect->async_ref->Set(expect->async_operation);

    pool_ref(expect->pool);
}

static void
expect_monitor_timeout(void *ctx)
{
    ExpectMonitor *expect =
        (ExpectMonitor *)ctx;
    expect->handler->Timeout();
    delete expect;
}

static void
expect_monitor_error(GError *error, void *ctx)
{
    ExpectMonitor *expect =
        (ExpectMonitor *)ctx;
    expect->handler->Error(error);
    delete expect;
}

static constexpr ConnectSocketHandler expect_monitor_handler = {
    .success = expect_monitor_success,
    .timeout = expect_monitor_timeout,
    .error = expect_monitor_error,
};

/*
 * lb_monitor_class
 *
 */

static void
expect_monitor_run(struct pool *pool, const struct lb_monitor_config *config,
                   SocketAddress address,
                   LBMonitorHandler &handler,
                   struct async_operation_ref *async_ref)
{
    ExpectMonitor *expect = new ExpectMonitor(pool, config,
                                              handler,
                                              async_ref);

    const unsigned connect_timeout = config->connect_timeout > 0
        ? config->connect_timeout
        : (config->timeout > 0
           ? config->timeout
           : 30);

    client_socket_new(*pool, address.GetFamily(), SOCK_STREAM, 0,
                      false,
                      SocketAddress::Null(),
                      address,
                      connect_timeout,
                      expect_monitor_handler, expect,
                      *async_ref);
}

const struct lb_monitor_class expect_monitor_class = {
    .run = expect_monitor_run,
};
