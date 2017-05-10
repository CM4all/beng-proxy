/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_MONITOR_CONTROLLER_HXX
#define BENG_PROXY_LB_MONITOR_CONTROLLER_HXX

#include "Monitor.hxx"
#include "Logger.hxx"
#include "event/TimerEvent.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "util/Cancellable.hxx"

struct pool;
class EventLoop;
class SocketAddress;
struct LbMonitorConfig;
struct LbMonitorClass;
class LbMonitorController;

class LbMonitorController final : public Logger, public LbMonitorHandler {
    EventLoop &event_loop;
    struct pool &pool;

    const std::string name;
    const LbMonitorConfig &config;
    const AllocatedSocketAddress address;
    const LbMonitorClass &class_;

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
    virtual void Error(GError *error) override;

protected:
    /* virtual methods from class Logger */
    std::string MakeLogName() const noexcept override {
        return "monitor " + name;
    }
};

LbMonitorController *
lb_monitor_new(EventLoop &event_loop, struct pool &pool, const char *name,
               const LbMonitorConfig &config,
               SocketAddress address,
               const LbMonitorClass &class_);

void
lb_monitor_free(LbMonitorController *monitor);

void
lb_monitor_enable(LbMonitorController *monitor);

#endif
