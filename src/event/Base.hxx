/*
 * C++ wrappers for libevent.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef EVENT_BASE_HXX
#define EVENT_BASE_HXX

#include <event.h>

class EventBase {
    struct event_base *event_base;

public:
    EventBase():event_base(::event_init()) {}
    ~EventBase() {
        ::event_base_free(event_base);
    }

    EventBase(const EventBase &other) = delete;
    EventBase &operator=(const EventBase &other) = delete;

    struct event_base *Get() {
        return event_base;
    }

    void Reinit() {
        event_reinit(event_base);
    }

    void Dispatch() {
        ::event_base_dispatch(event_base);
    }

    bool LoopOnce(bool non_block=false) {
        int flags = EVLOOP_ONCE;
        if (non_block)
            flags |= EVLOOP_NONBLOCK;
        return ::event_loop(flags) == 0;
    }

    void Break() {
        ::event_base_loopbreak(event_base);
    }
};

#endif
