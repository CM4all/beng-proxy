/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_stopwatch.hxx"
#include "istream/ForwardIstream.hxx"
#include "stopwatch.h"

class StopwatchIstream final : public ForwardIstream {
    struct stopwatch &stopwatch;

public:
    StopwatchIstream(struct pool &p, struct istream &_input,
                     struct stopwatch &_stopwatch)
        :ForwardIstream(p, _input,
                        MakeIstreamHandler<StopwatchIstream>::handler, this),
         stopwatch(_stopwatch) {}

    /* virtual methods from class Istream */

    int AsFd() override;

    /* handler */

    void OnEof();
    void OnError(GError *error);
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
StopwatchIstream::AsFd()
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

struct istream *
istream_stopwatch_new(struct pool *pool, struct istream *input,
                      struct stopwatch *_stopwatch)
{
    if (_stopwatch == nullptr)
        return input;

    return NewIstream<StopwatchIstream>(*pool, *input, *_stopwatch);
}
