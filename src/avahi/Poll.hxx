/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef AVAHI_POLL_HXX
#define AVAHI_POLL_HXX

#include <avahi-common/watch.h>

class EventLoop;

class MyAvahiPoll final : public AvahiPoll {
    EventLoop &event_loop;

public:
    explicit MyAvahiPoll(EventLoop &_loop);

    MyAvahiPoll(const MyAvahiPoll &) = delete;
    MyAvahiPoll &operator=(const MyAvahiPoll &) = delete;

private:
    static AvahiWatch *WatchNew(const AvahiPoll *api, int fd,
                                AvahiWatchEvent event,
                                AvahiWatchCallback callback,
                                void *userdata);

    static AvahiTimeout *TimeoutNew(const AvahiPoll *api,
                                    const struct timeval *tv,
                                    AvahiTimeoutCallback callback,
                                    void *userdata);
};

#endif
