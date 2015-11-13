/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_stopwatch.hxx"
#include "istream/ForwardIstream.hxx"
#include "stopwatch.hxx"

class StopwatchIstream final : public ForwardIstream {
    struct stopwatch &stopwatch;

public:
    StopwatchIstream(struct pool &p, Istream &_input,
                     struct stopwatch &_stopwatch)
        :ForwardIstream(p, _input),
         stopwatch(_stopwatch) {}

    /* virtual methods from class Istream */

    int _AsFd() override;

    /* virtual methods from class IstreamHandler */
    void OnEof() override;
    void OnError(GError *error) override;
};


/*
 * istream handler
 *
 */

void
StopwatchIstream::OnEof()
{
    stopwatch_event(&stopwatch, "end");
    stopwatch_dump(&stopwatch);

    ForwardIstream::OnEof();
}

void
StopwatchIstream::OnError(GError *error)
{
    stopwatch_event(&stopwatch, "abort");
    stopwatch_dump(&stopwatch);

    ForwardIstream::OnError(error);
}

/*
 * istream implementation
 *
 */

int
StopwatchIstream::_AsFd()
{
    int fd = input.AsFd();
    if (fd >= 0) {
        stopwatch_event(&stopwatch, "as_fd");
        stopwatch_dump(&stopwatch);
        Destroy();
    }

    return fd;
}

/*
 * constructor
 *
 */

Istream *
istream_stopwatch_new(struct pool &pool, Istream &input,
                      struct stopwatch *_stopwatch)
{
    if (_stopwatch == nullptr)
        return &input;

    return NewIstream<StopwatchIstream>(pool, input, *_stopwatch);
}
