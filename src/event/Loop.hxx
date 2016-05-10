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

    bool Loop(int flags) {
        return ::event_base_loop(event_base, flags) == 0;
    }

    bool LoopOnce(bool non_block=false) {
        int flags = EVLOOP_ONCE;
        if (non_block)
            flags |= EVLOOP_NONBLOCK;
        return Loop(flags);
    }

    void Break() {
        ::event_base_loopbreak(event_base);
    }

    void DumpEvents(FILE *file) {
        event_base_dump_events(event_base, file);
    }
};

#endif
