/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_MONITOR_CONTROLLER_HXX
#define BENG_PROXY_LB_MONITOR_CONTROLLER_HXX

#include "Monitor.hxx"
#include "io/Logger.hxx"
#include "event/TimerEvent.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "util/Cancellable.hxx"

struct pool;
class EventLoop;
class SocketAddress;
struct LbMonitorConfig;
struct LbMonitorClass;
class LbMonitorController;

class LbMonitorController final : public LbMonitorHandler {
    EventLoop &event_loop;
    struct pool &pool;

    const std::string name;
    const LbMonitorConfig &config;
    const AllocatedSocketAddress address;
    const LbMonitorClass &class_;

    const Logger logger;

    const struct timeval interval;
    TimerEvent interval_event;

    const struct timeval timeout;
    TimerEvent timeout_event;

    CancellablePointer cancel_ptr;

    bool state = true;
    bool fade = false;

public:
    LbMonitorController(EventLoop &_event_loop, struct pool &_pool, const char *_name,
                        const LbMonitorConfig &_config,
                        SocketAddress _address,
                        const LbMonitorClass &_class);

    ~LbMonitorController();

    void Enable() {
        static constexpr struct timeval immediately = { 0, 0 };
        interval_event.Add(immediately);
    }

private:
    void IntervalCallback();
    void TimeoutCallback();

    /* virtual methods from class LbMonitorHandler */
    virtual void Success() override;
    virtual void Fade() override;
    virtual void Timeout() override;
    virtual void Error(std::exception_ptr e) override;
};

#endif
