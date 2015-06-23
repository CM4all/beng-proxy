/*
 * Easy deferral of function calls.  Internally, this uses an event
 * struct with a zero timeout.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_DEFER_EVENT_HXX
#define BENG_PROXY_DEFER_EVENT_HXX

#include <event.h>

struct DeferEvent {
    struct event event;

    void Init(void (*callback)(evutil_socket_t, short, void *), void *ctx) {
        evtimer_set(&event, callback, ctx);
    }

    void Deinit() {
        Cancel();
    }

    void Add() {
        const struct timeval tv = { 0, 0};
        evtimer_add(&event, &tv);
    }

    void Cancel() {
        evtimer_del(&event);
    }
};

#endif
