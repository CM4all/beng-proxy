/*
 * C++ wrappers for libevent.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef EVENT_BASE_HXX
#define EVENT_BASE_HXX

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
        ::event_base_dispatch(event_base);
    }

    bool LoopNonBlock() {
        return Loop(EVLOOP_NONBLOCK);
    }

    bool LoopOnce() {
        return Loop(EVLOOP_ONCE);
    }

    bool LoopOnceNonBlock() {
        return Loop(EVLOOP_ONCE|EVLOOP_NONBLOCK);
    }

    void Break() {
        ::event_base_loopbreak(event_base);
    }

    void DumpEvents(FILE *file) {
        event_base_dump_events(event_base, file);
    }

private:
    bool Loop(int flags) {
        return ::event_base_loop(event_base, flags) == 0;
    }
};

#endif
