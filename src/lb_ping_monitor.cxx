/*
 * Ping (ICMP) monitor class.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_ping_monitor.hxx"
#include "lb_monitor.hxx"
#include "ping.hxx"
#include "pool.hxx"
#include "net/SocketAddress.hxx"

class LbPingClientHandler final : public PingClientHandler {
    LbMonitorHandler &handler;

public:
    explicit LbPingClientHandler(LbMonitorHandler &_handler)
        :handler(_handler) {}

    void PingResponse() override {
        handler.Success();
    }

    void PingTimeout() override {
        handler.Timeout();
    }

    void PingError(GError *error) override {
        handler.Error(error);
    }
};

static void
ping_monitor_run(struct pool &pool,
                 gcc_unused const LbMonitorConfig &config,
                 SocketAddress address,
                 LbMonitorHandler &handler,
                 struct async_operation_ref &async_ref)
{
    ping(pool, address,
         *NewFromPool<LbPingClientHandler>(pool, handler),
         async_ref);
}

const LbMonitorClass ping_monitor_class = {
    .run = ping_monitor_run,
};
