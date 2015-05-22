/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_STOPWATCH_HXX
#define BENG_PROXY_ISTREAM_STOPWATCH_HXX

struct pool;
struct istream;
struct stopwatch;

#ifdef ENABLE_STOPWATCH

/**
 * This istream filter emits a stopwatch event and dump on eof/abort.
 */
struct istream *
istream_stopwatch_new(struct pool *pool, struct istream *input,
                      struct stopwatch *_stopwatch);

#else /* !ENABLE_STOPWATCH */

static inline struct istream *
istream_stopwatch_new(struct pool *pool, struct istream *input,
                      struct stopwatch *_stopwatch)
{
    (void)pool;
    (void)_stopwatch;

    return input;
}

#endif /* !ENABLE_STOPWATCH */

#endif
