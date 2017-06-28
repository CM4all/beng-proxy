/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "TimeoutIstream.hxx"
#include "ForwardIstream.hxx"
#include "event/TimerEvent.hxx"
#include "gerrno.h"

#include <errno.h>

class TimeoutIstream final : public ForwardIstream {
    TimerEvent timeout_event;

    const struct timeval *timeout;

public:
    explicit TimeoutIstream(struct pool &p, Istream &_input,
                            EventLoop &event_loop,
                            const struct timeval &_timeout)
        :ForwardIstream(p, _input),
         timeout_event(event_loop, BIND_THIS_METHOD(OnTimeout)),
         timeout(&_timeout) {}

    ~TimeoutIstream() {
        timeout_event.Cancel();
    }

private:
    void OnTimeout() {
        auto error = g_error_new_literal(errno_quark(), ETIMEDOUT, "timeout");
        input.Close();
        DestroyError(error);
    }

public:
    /* virtual methods from class Istream */

    void _Read() override {
        if (timeout != nullptr) {
            /* enable the timeout on the first Read() call (if one was
               specified) */
            timeout_event.Add(*timeout);
            timeout = nullptr;
        }

        ForwardIstream::_Read();
    }

    /* virtual methods from class IstreamHandler */

    size_t OnData(const void *data, size_t length) override {
        /* disable the timeout as soon as the first data byte
           arrives */
        timeout_event.Cancel();

        return ForwardIstream::OnData(data, length);
    }

    ssize_t OnDirect(FdType type, int fd, size_t max_length) override {
        /* disable the timeout as soon as the first data byte
           arrives */
        timeout_event.Cancel();

        return ForwardIstream::OnDirect(type, fd, max_length);
    }
};

Istream *
NewTimeoutIstream(struct pool &pool, Istream &input,
                  EventLoop &event_loop,
                  const struct timeval &timeout)
{
    return NewIstream<TimeoutIstream>(pool, input, event_loop, timeout);
}
