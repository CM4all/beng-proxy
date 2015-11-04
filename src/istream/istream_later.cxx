/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_later.hxx"
#include "ForwardIstream.hxx"
#include "event/DeferEvent.hxx"
#include "event/Callback.hxx"

#include <event.h>

class LaterIstream final : public ForwardIstream {
    DeferEvent defer_event;

public:
    LaterIstream(struct pool &_pool, Istream &_input)
        :ForwardIstream(_pool, _input,
                        MakeIstreamHandler<LaterIstream>::handler, this),
         defer_event(MakeSimpleEventCallback(LaterIstream, EventCallback),
                     this)
    {
    }

    /* virtual methods from class Istream */

    off_t _GetAvailable(gcc_unused bool partial) override {
        return -1;
    }

    off_t _Skip(gcc_unused off_t length) override {
        return -1;
    }

    void _Read() override {
        Schedule();
    }

    int _AsFd() override {
        return -1;
    }

    void _Close() override {
        defer_event.Deinit();

        /* input can only be nullptr during the eof callback delay */
        if (HasInput())
            input.Close();

        Destroy();
    }

    /* handler */
    void OnEof() {
        ClearInput();
        Schedule();
    }

    void OnError(GError *error) {
        defer_event.Deinit();
        ForwardIstream::OnError(error);
    }

private:
    void Schedule() {
        defer_event.Add();
    }

    void EventCallback() {
        if (!HasInput())
            DestroyEof();
        else
            ForwardIstream::_Read();
    }
};

Istream *
istream_later_new(struct pool *pool, Istream &input)
{
    return NewIstream<LaterIstream>(*pool, input);
}
