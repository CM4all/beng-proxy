/*
 * An istream facade which holds an optional istream.  It blocks until
 * it is told to resume or to discard the inner istream.  Errors are
 * reported to the handler immediately.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_optional.hxx"
#include "ForwardIstream.hxx"
#include "istream_null.hxx"

#include <assert.h>

class OptionalIstream final : public ForwardIstream {
    bool resumed = false;

public:
    OptionalIstream(struct pool &p, struct istream &_input)
        :ForwardIstream(p, _input,
                        MakeIstreamHandler<OptionalIstream>::handler, this) {}

    void Resume() {
        resumed = true;
    }

    void Discard() {
        assert(!resumed);

        resumed = true;

        /* replace the input with a "null" istream */
        ReplaceInputDirect(*istream_null_new(&GetPool()),
                           MakeIstreamHandler<OptionalIstream>::handler, this);
    }

    /* virtual methods from class Istream */

    off_t GetAvailable(bool partial) override {
        /* can't respond to this until we're resumed, because the
           original input can be discarded */
        return resumed ? ForwardIstream::GetAvailable(partial) : -1;
    }

    void Read() override {
        if (resumed)
            ForwardIstream::Read();
    }

    int AsFd() override {
        return resumed
            ? ForwardIstream::AsFd()
            : -1;
    }

    /* handler */

    size_t OnData(const void *data, size_t length) {
        return resumed ? InvokeData(data, length) : 0;
    }
};

struct istream *
istream_optional_new(struct pool *pool, struct istream *input)
{
    return NewIstream<OptionalIstream>(*pool, *input);
}

void
istream_optional_resume(struct istream *istream)
{
    auto &optional = (OptionalIstream &)Istream::Cast(*istream);
    optional.Resume();
}

void
istream_optional_discard(struct istream *istream)
{
    auto &optional = (OptionalIstream &)Istream::Cast(*istream);
    optional.Discard();
}
