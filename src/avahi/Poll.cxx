/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Poll.hxx"
#include "event/SocketEvent.hxx"
#include "event/TimerEvent.hxx"

static unsigned
FromAvahiWatchEvent(AvahiWatchEvent e)
{
    // TODO: what about AVAHI_WATCH_ERR and AVAHI_WATCH_HUP?

    return (e & AVAHI_WATCH_IN ? EV_READ : 0) |
        (e & AVAHI_WATCH_OUT ? EV_WRITE : 0);
}

static AvahiWatchEvent
ToAvahiWatchEvent(unsigned events)
{
    // TODO: what about AVAHI_WATCH_ERR and AVAHI_WATCH_HUP?

    return AvahiWatchEvent((events & EV_READ ? AVAHI_WATCH_IN : 0) |
                           (events & EV_WRITE ? AVAHI_WATCH_OUT : 0));
}

struct AvahiWatch final {
private:
    SocketEvent event;

    const AvahiWatchCallback callback;
    void *const userdata;

    AvahiWatchEvent received = AvahiWatchEvent(0);

public:
    AvahiWatch(EventLoop &_loop, int _fd, AvahiWatchEvent _event,
               AvahiWatchCallback _callback, void *_userdata)
        :event(_loop, _fd, FromAvahiWatchEvent(_event),
               BIND_THIS_METHOD(OnSocketReady)),
         callback(_callback), userdata(_userdata) {
        event.Add();
    }

    ~AvahiWatch() {
        event.Delete();
    }

    static void WatchUpdate(AvahiWatch *w, AvahiWatchEvent _event) {
        w->event.Delete();
        w->event.Set(w->event.GetFd(), FromAvahiWatchEvent(_event));
        w->event.Add();
    }

    static AvahiWatchEvent WatchGetEvents(AvahiWatch *w) {
        return w->received;
    }

    static void WatchFree(AvahiWatch *w) {
        delete w;
    }

protected:
    void OnSocketReady(unsigned events) {
        received = ToAvahiWatchEvent(events);
        callback(this, event.GetFd(), received, userdata);
        received = AvahiWatchEvent(0);
        event.Add();
    }
};

struct AvahiTimeout final {
private:
    TimerEvent event;
    const AvahiTimeoutCallback callback;
    void *const userdata;

public:
    AvahiTimeout(EventLoop &_loop, const struct timeval *tv,
                 AvahiTimeoutCallback _callback, void *_userdata)
        :event(_loop, BIND_THIS_METHOD(OnTimeout)),
         callback(_callback), userdata(_userdata) {
        if (tv != nullptr)
            event.Add(*tv);
    }

    ~AvahiTimeout() {
        event.Cancel();
    }

    static void TimeoutUpdate(AvahiTimeout *t, const struct timeval *tv) {
        if (tv != nullptr)
            t->event.Add(*tv);
        else
            t->event.Cancel();
    }

    static void TimeoutFree(AvahiTimeout *t) {
        delete t;
    }

protected:
    virtual void OnTimeout() {
        callback(this, userdata);
    }
};

MyAvahiPoll::MyAvahiPoll(EventLoop &_loop):event_loop(_loop)
{
    watch_new = WatchNew;
    watch_update = AvahiWatch::WatchUpdate;
    watch_get_events = AvahiWatch::WatchGetEvents;
    watch_free = AvahiWatch::WatchFree;
    timeout_new = TimeoutNew;
    timeout_update = AvahiTimeout::TimeoutUpdate;
    timeout_free = AvahiTimeout::TimeoutFree;
}

AvahiWatch *
MyAvahiPoll::WatchNew(const AvahiPoll *api, int fd, AvahiWatchEvent event,
                      AvahiWatchCallback callback, void *userdata) {
    const MyAvahiPoll &poll = *(const MyAvahiPoll *)api;

    return new AvahiWatch(poll.event_loop, fd, event, callback, userdata);
}

AvahiTimeout *
MyAvahiPoll::TimeoutNew(const AvahiPoll *api, const struct timeval *tv,
                        AvahiTimeoutCallback callback, void *userdata) {
    const MyAvahiPoll &poll = *(const MyAvahiPoll *)api;

    return new AvahiTimeout(poll.event_loop, tv, callback, userdata);
}
