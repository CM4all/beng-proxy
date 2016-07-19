/*
 * Monitor which attempts to establish a TCP connection.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_syn_monitor.hxx"
#include "lb_monitor.hxx"
#include "lb_config.hxx"
#include "pool.hxx"
#include "net/ConnectSocket.hxx"
#include "net/SocketAddress.hxx"
#include "util/Cancellable.hxx"

#include <unistd.h>
#include <sys/socket.h>

class LbSynMonitor final : public ConnectSocketHandler {
    LbMonitorHandler &handler;

public:
    explicit LbSynMonitor(LbMonitorHandler &_handler):handler(_handler) {}

    /* virtual methods from class ConnectSocketHandler */
    void OnSocketConnectSuccess(gcc_unused SocketDescriptor &&fd) override {
        /* ignore the socket, we don't need it */

        handler.Success();
    }

    void OnSocketConnectTimeout() override {
        handler.Timeout();
    }

    void OnSocketConnectError(GError *error) override {
        handler.Error(error);
    }
};

/*
 * lb_monitor_class
 *
 */

static void
syn_monitor_run(EventLoop &event_loop, struct pool &pool,
                const LbMonitorConfig &config,
                SocketAddress address,
                LbMonitorHandler &handler,
                CancellablePointer &cancel_ptr)
{
    const unsigned timeout = config.timeout > 0
        ? config.timeout
        : 30;

    auto *syn = NewFromPool<LbSynMonitor>(pool, handler);
    client_socket_new(event_loop, pool, address.GetFamily(), SOCK_STREAM, 0,
                      false,
                      SocketAddress::Null(),
                      address,
                      timeout,
                      *syn,
                      cancel_ptr);
}

const LbMonitorClass syn_monitor_class = {
    .run = syn_monitor_run,
};
