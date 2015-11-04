/*
 * istream facade that ignores read() calls until it is resumed.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_pause.hxx"
#include "ForwardIstream.hxx"

class PauseIstream final : public ForwardIstream {
    bool resumed = false;

public:
    PauseIstream(struct pool &p, Istream &_input)
        :ForwardIstream(p, _input,
                        MakeIstreamHandler<PauseIstream>::handler, this) {}

    void Resume() {
        resumed = true;
    }

    /* virtual methods from class Istream */

    void _Read() override {
        if (resumed)
            ForwardIstream::_Read();
        else
            CopyDirect();
    }

    int _AsFd() override {
        return resumed
            ? ForwardIstream::_AsFd()
            : -1;
    }
};

Istream *
istream_pause_new(struct pool *pool, Istream &input)
{
    return NewIstream<PauseIstream>(*pool, input);
}

void
istream_pause_resume(Istream &istream)
{
    auto &pause = (PauseIstream &)istream;
    pause.Resume();
}
