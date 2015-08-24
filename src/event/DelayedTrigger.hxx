/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_DELAYED_TRIGGER_HXX
#define BENG_PROXY_DELAYED_TRIGGER_HXX

#include "Event.hxx"

/**
 * Invoke a callback after a certain delay.
 */
class DelayedTrigger {
    Event event;

    struct timeval tv;

public:
    DelayedTrigger(event_callback_fn callback, void *ctx, unsigned delay_s) {
        event.SetTimer(callback, ctx);

        tv.tv_sec = delay_s;
        tv.tv_usec = 0;
    }

    ~DelayedTrigger() {
        event.Delete();
    }

    void Trigger() {
        if (!event.IsTimerPending())
            event.Add(&tv);
    }

    void Cancel() {
        event.Delete();
    }
};

#endif
