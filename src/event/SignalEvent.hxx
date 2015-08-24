/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SIGNAL_EVENT_HXX
#define SIGNAL_EVENT_HXX

#include "Event.hxx"

class SignalEvent {
    Event event;

public:
    void Set(int sig, event_callback_fn callback, void *ctx) {
        event.SetSignal(sig, callback, ctx);
    }

    void Add(const struct timeval *timeout=nullptr) {
        event.Add(timeout);
    }

    void Delete() {
        event.Delete();
    }
};

#endif
