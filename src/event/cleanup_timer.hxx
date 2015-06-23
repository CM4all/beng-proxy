/*
 * Wrapper for event.h which aims to simplify installing recurring
 * events.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CLEANUP_TIMER_HXX
#define BENG_PROXY_CLEANUP_TIMER_HXX

#include <event.h>

class CleanupTimer {
    struct event event;

    struct timeval delay;

    /**
     * @return true if another cleanup shall be scheduled
     */
    bool (*callback)(void *ctx);
    void *callback_ctx;

public:
    void Init(unsigned delay_s,
              bool (*callback)(void *ctx), void *ctx);
    void Deinit() {
        Disable();
    }

    void Enable();
    void Disable();

private:
    static void Callback(int fd, short event, void *ctx);
};

#endif
