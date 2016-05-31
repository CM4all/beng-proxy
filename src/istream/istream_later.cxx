/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_later.hxx"
#include "ForwardIstream.hxx"
#include "event/DeferEvent.hxx"
#include "event/Callback.hxx"

class LaterIstream final : public ForwardIstream, DeferEvent {
public:
    LaterIstream(struct pool &_pool, Istream &_input, EventLoop &event_loop)
        :ForwardIstream(_pool, _input),
         DeferEvent(event_loop)
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
        DeferEvent::Cancel();

        /* input can only be nullptr during the eof callback delay */
        if (HasInput())
            input.Close();

        Destroy();
    }

    /* virtual methods from class IstreamHandler */

    void OnEof() override {
        ClearInput();
        Schedule();
    }

    void OnError(GError *error) override {
        DeferEvent::Cancel();
        ForwardIstream::OnError(error);
    }

private:
    void Schedule() {
        DeferEvent::Schedule();
    }

    /* virtual methods from class DeferEvent */
    void OnDeferred() override {
        if (!HasInput())
            DestroyEof();
        else
            ForwardIstream::_Read();
    }
};

Istream *
istream_later_new(struct pool &pool, Istream &input, EventLoop &event_loop)
{
    return NewIstream<LaterIstream>(pool, input, event_loop);
}
