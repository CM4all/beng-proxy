/*
 * C++ wrappers for libevent.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef EVENT_BASE_HXX
#define EVENT_BASE_HXX

#include "LightDeferEvent.hxx"

#include <event.h>

class EventLoop {
    struct event_base *const event_base;

    static struct event_base *Create() {
#ifndef NDEBUG
        /* call event_enable_debug_mode() only once, before the first
           event_init() call */
        static struct DebugMode {
            DebugMode() {
                event_enable_debug_mode();
            }
        } once_enable_debug_mode;
#endif

        return ::event_init();
    }

    boost::intrusive::list<LightDeferEvent,
                           boost::intrusive::member_hook<LightDeferEvent,
                                                         LightDeferEvent::SiblingsHook,
                                                         &LightDeferEvent::siblings>,
                           boost::intrusive::constant_time_size<false>> defer;

public:
    EventLoop():event_base(Create()) {}

    ~EventLoop() {
        ::event_base_free(event_base);
    }

    EventLoop(const EventLoop &other) = delete;
    EventLoop &operator=(const EventLoop &other) = delete;

    struct event_base *Get() {
        return event_base;
    }

    void Reinit() {
        event_reinit(event_base);
    }

    void Dispatch() {
        RunDeferred();
        while (Loop(EVLOOP_ONCE))
            RunDeferred();
    }

    bool LoopNonBlock() {
        return RunDeferred() && Loop(EVLOOP_NONBLOCK) && RunDeferred();
    }

    bool LoopOnce() {
        return RunDeferred() && Loop(EVLOOP_ONCE) && RunDeferred();
    }

    bool LoopOnceNonBlock() {
        return RunDeferred() && Loop(EVLOOP_ONCE|EVLOOP_NONBLOCK) &&
            RunDeferred();
    }

    void Break() {
        ::event_base_loopbreak(event_base);
    }

    void DumpEvents(FILE *file) {
        event_base_dump_events(event_base, file);
    }

    void Defer(LightDeferEvent &e);
    void CancelDefer(LightDeferEvent &e);

private:
    bool Loop(int flags) {
        return ::event_base_loop(event_base, flags) == 0;
    }

    bool RunDeferred();
};

#endif
