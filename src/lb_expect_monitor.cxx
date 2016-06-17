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
#include "event/SocketEvent.hxx"

#include <unistd.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>

struct ExpectMonitor final : ConnectSocketHandler {
    struct pool &pool;
    const LbMonitorConfig &config;

    int fd;

    SocketEvent event;

    LbMonitorHandler &handler;

    struct async_operation_ref &async_ref;
    struct async_operation operation;

    ExpectMonitor(EventLoop &event_loop,
                  struct pool &_pool, const LbMonitorConfig &_config,
                  LbMonitorHandler &_handler,
                  async_operation_ref &_async_ref)
        :pool(_pool), config(_config),
         event(event_loop, BIND_THIS_METHOD(EventCallback)),
         handler(_handler),
         async_ref(_async_ref) {}

    ExpectMonitor(const ExpectMonitor &other) = delete;

    void Abort();

    /* virtual methods from class ConnectSocketHandler */
    void OnSocketConnectSuccess(SocketDescriptor &&fd) override;

    void OnSocketConnectTimeout() override {
        handler.Timeout();
        delete this;
    }

    void OnSocketConnectError(GError *error) override {
        handler.Error(error);
        delete this;
    }

private:
    void EventCallback(short events);
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

inline void
ExpectMonitor::Abort()
{
    event.Delete();
    close(fd);
    pool_unref(&pool);
    delete this;
}

/*
 * libevent callback
 *
 */

inline void
ExpectMonitor::EventCallback(short events)
{
    operation.Finished();

    if (events & EV_TIMEOUT) {
        close(fd);
        handler.Timeout();
    } else {
        char buffer[1024];

        ssize_t nbytes = recv(fd, buffer, sizeof(buffer),
                              MSG_DONTWAIT);
        if (nbytes < 0) {
            GError *error = new_error_errno();
            close(fd);
            handler.Error(error);
        } else if (!config.fade_expect.empty() &&
                   check_expectation(buffer, nbytes,
                                     config.fade_expect.c_str())) {
            close(fd);
            handler.Fade();
        } else if (config.expect.empty() ||
                   check_expectation(buffer, nbytes,
                                     config.expect.c_str())) {
            close(fd);
            handler.Success();
        } else {
            close(fd);
            GError *error = g_error_new_literal(g_file_error_quark(), 0,
                                                "Expectation failed");
            handler.Error(error);
        }
    }

    pool_unref(&pool);
    delete this;
    pool_commit();
}

/*
 * client_socket handler
 *
 */

void
ExpectMonitor::OnSocketConnectSuccess(SocketDescriptor &&new_fd)
{
    if (!config.send.empty()) {
        ssize_t nbytes = send(new_fd.Get(), config.send.data(),
                              config.send.length(),
                              MSG_DONTWAIT);
        if (nbytes < 0) {
            GError *error = new_error_errno();
            handler.Error(error);
            return;
        }
    }

    struct timeval expect_timeout = {
        time_t(config.timeout > 0 ? config.timeout : 10),
        0,
    };

    fd = new_fd.Steal();
    event.Set(fd, EV_READ|EV_TIMEOUT);
    event.Add(expect_timeout);

    operation.Init2<ExpectMonitor>();
    async_ref.Set(operation);

    pool_ref(&pool);
}

/*
 * lb_monitor_class
 *
 */

static void
expect_monitor_run(EventLoop &event_loop, struct pool &pool,
                   const LbMonitorConfig &config,
                   SocketAddress address,
                   LbMonitorHandler &handler,
                   struct async_operation_ref &async_ref)
{
    ExpectMonitor *expect = new ExpectMonitor(event_loop, pool, config,
                                              handler,
                                              async_ref);

    const unsigned connect_timeout = config.connect_timeout > 0
        ? config.connect_timeout
        : (config.timeout > 0
           ? config.timeout
           : 30);

    client_socket_new(pool, address.GetFamily(), SOCK_STREAM, 0,
                      false,
                      SocketAddress::Null(),
                      address,
                      connect_timeout,
                      *expect, async_ref);
}

const LbMonitorClass expect_monitor_class = {
    .run = expect_monitor_run,
};
