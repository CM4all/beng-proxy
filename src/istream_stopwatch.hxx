/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_STOPWATCH_HXX
#define BENG_PROXY_ISTREAM_STOPWATCH_HXX

struct pool;
class Istream;
struct Stopwatch;

#ifdef ENABLE_STOPWATCH

/**
 * This istream filter emits a stopwatch event and dump on eof/abort.
 */
Istream *
istream_stopwatch_new(struct pool &pool, Istream &input,
                      Stopwatch *_stopwatch);

#else /* !ENABLE_STOPWATCH */

static inline Istream *
istream_stopwatch_new(struct pool &pool, Istream &input,
                      Stopwatch *_stopwatch)
{
    (void)pool;
    (void)_stopwatch;

    return &input;
}

#endif /* !ENABLE_STOPWATCH */

#endif
