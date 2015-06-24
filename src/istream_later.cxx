/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_later.hxx"
#include "istream_forward.hxx"
#include "istream_internal.hxx"
#include "istream_forward.hxx"
#include "event/DeferEvent.hxx"
#include "event/Callback.hxx"
#include "util/Cast.hxx"
#include "pool.hxx"

#include <event.h>

class LaterIstream : public ForwardIstream {
    DeferEvent defer_event;

public:
    LaterIstream(struct pool &pool, struct istream &_input)
        :ForwardIstream(pool, _input,
                        MakeIstreamHandler<LaterIstream>::handler, this)
    {
        defer_event.Init(MakeSimpleEventCallback(LaterIstream, EventCallback),
                         this);
    }

    /* virtual methods from class Istream */

    off_t GetAvailable(gcc_unused bool partial) override {
        return -1;
    }

    off_t Skip(gcc_unused off_t length) override {
        return -1;
    }

    void Read() override {
        Schedule();
    }

    int AsFd() override {
        return -1;
    }

    void Close() override {
        defer_event.Deinit();

        /* input can only be nullptr during the eof callback delay */
        if (HasInput())
            input.CloseHandler();

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
            ForwardIstream::Read();
    }
};

struct istream *
istream_later_new(struct pool *pool, struct istream *input)
{
    assert(input != nullptr);
    assert(!istream_has_handler(input));

    return NewIstream<LaterIstream>(*pool, *input);
}
