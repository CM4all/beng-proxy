/*
 * Copyright 2007-2018 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Poll.hxx"
#include "event/SocketEvent.hxx"
#include "event/TimerEvent.hxx"

static unsigned
FromAvahiWatchEvent(AvahiWatchEvent e)
{
    // TODO: what about AVAHI_WATCH_ERR and AVAHI_WATCH_HUP?

    return (e & AVAHI_WATCH_IN ? SocketEvent::READ : 0) |
        (e & AVAHI_WATCH_OUT ? SocketEvent::WRITE : 0);
}

static AvahiWatchEvent
ToAvahiWatchEvent(unsigned events)
{
    // TODO: what about AVAHI_WATCH_ERR and AVAHI_WATCH_HUP?

    return AvahiWatchEvent((events & SocketEvent::READ ? AVAHI_WATCH_IN : 0) |
                           (events & SocketEvent::WRITE ? AVAHI_WATCH_OUT : 0));
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
        :event(_loop, BIND_THIS_METHOD(OnSocketReady), SocketDescriptor(_fd)),
         callback(_callback), userdata(_userdata) {
        event.Schedule(FromAvahiWatchEvent(_event));
    }

    static void WatchUpdate(AvahiWatch *w, AvahiWatchEvent _event) {
        w->event.Schedule(FromAvahiWatchEvent(_event));
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
        callback(this, event.GetSocket().Get(), received, userdata);
        received = AvahiWatchEvent(0);
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
