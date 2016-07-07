/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_delayed.hxx"
#include "ForwardIstream.hxx"
#include "async.hxx"

#include <assert.h>
#include <string.h>

class DelayedIstream final : public ForwardIstream {
    CancellablePointer cancel_ptr;

public:
    explicit DelayedIstream(struct pool &p)
        :ForwardIstream(p) {
    }

    CancellablePointer &GetCancellablePointer() {
        return cancel_ptr;
    }

    void Set(Istream &_input) {
        assert(!HasInput());

        SetInput(_input, GetHandlerDirect());
    }

    void SetEof() {
        assert(!HasInput());

        DestroyEof();
    }

    void SetError(GError *error) {
        assert(!HasInput());

        DestroyError(error);
    }

    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) override {
        return HasInput()
            ? ForwardIstream::_GetAvailable(partial)
            : -1;
    }

    void _Read() override {
        if (HasInput())
            ForwardIstream::_Read();
    }

    int _AsFd() override {
        return HasInput()
            ? ForwardIstream::_AsFd()
            : -1;
    }

    void _Close() override {
        if (HasInput())
            ForwardIstream::_Close();
        else {
            if (cancel_ptr)
                cancel_ptr.Cancel();

            Destroy();
        }
    }
};

Istream *
istream_delayed_new(struct pool *pool)
{
    return NewIstream<DelayedIstream>(*pool);
}

CancellablePointer &
istream_delayed_cancellable_ptr(Istream &i_delayed)
{
    auto &delayed = (DelayedIstream &)i_delayed;

    return delayed.GetCancellablePointer();
}

void
istream_delayed_set(Istream &i_delayed, Istream &input)
{
    auto &delayed = (DelayedIstream &)i_delayed;

    delayed.Set(input);
}

void
istream_delayed_set_eof(Istream &i_delayed)
{
    auto &delayed = (DelayedIstream &)i_delayed;

    delayed.SetEof();
}

void
istream_delayed_set_abort(Istream &i_delayed, GError *error)
{
    auto &delayed = (DelayedIstream &)i_delayed;

    delayed.SetError(error);
}
