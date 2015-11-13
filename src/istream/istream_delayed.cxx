/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_delayed.hxx"
#include "ForwardIstream.hxx"
#include "async.hxx"

#include <assert.h>
#include <string.h>

class DelayedIstream final : public ForwardIstream {
    struct async_operation_ref async;

public:
    explicit DelayedIstream(struct pool &p)
        :ForwardIstream(p) {
    }

    struct async_operation_ref &GetAsyncRef() {
        return async;
    }

    void Set(Istream &_input) {
        assert(!HasInput());

        async.Poison();
        SetInput(_input, GetHandlerDirect());
    }

    void SetEof() {
        assert(!HasInput());

        async.Poison();
        DestroyEof();
    }

    void SetError(GError *error) {
        assert(!HasInput());

        async.Poison();
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
            if (async.IsDefined())
                async.Abort();

            Destroy();
        }
    }
};

Istream *
istream_delayed_new(struct pool *pool)
{
    return NewIstream<DelayedIstream>(*pool);
}

struct async_operation_ref *
istream_delayed_async_ref(Istream &i_delayed)
{
    auto &delayed = (DelayedIstream &)i_delayed;

    return &delayed.GetAsyncRef();
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
