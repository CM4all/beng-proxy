/*
 * istream facade that ignores read() calls until it is resumed.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_pause.hxx"
#include "istream_forward.hxx"

class PauseIstream final : public ForwardIstream {
    bool resumed = false;

public:
    PauseIstream(struct pool &p, struct istream &_input)
        :ForwardIstream(p, _input,
                        MakeIstreamHandler<PauseIstream>::handler, this) {}

    void Resume() {
        resumed = true;
    }

    /* virtual methods from class Istream */

    void Read() override {
        if (resumed)
            ForwardIstream::Read();
        else
            CopyDirect();
    }

    int AsFd() override {
        return resumed
            ? ForwardIstream::AsFd()
            : -1;
    }
};

struct istream *
istream_pause_new(struct pool *pool, struct istream *input)
{
    return NewIstream<PauseIstream>(*pool, *input);
}

void
istream_pause_resume(struct istream *istream)
{
    assert(istream != nullptr);

    auto &pause = (PauseIstream &)Istream::Cast(*istream);
    pause.Resume();
}
