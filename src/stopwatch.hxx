/*
 * Statistics collector.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STOPWATCH_HXX
#define BENG_PROXY_STOPWATCH_HXX

#include <stddef.h>

struct pool;
struct Stopwatch;
struct sockaddr;

#ifdef ENABLE_STOPWATCH

void
stopwatch_enable(void);

Stopwatch *
stopwatch_new(struct pool *pool, const char *name, const char *suffix=nullptr);

Stopwatch *
stopwatch_sockaddr_new(struct pool *pool, const struct sockaddr *address,
                       size_t address_length, const char *suffix);

Stopwatch *
stopwatch_fd_new(struct pool *pool, int fd, const char *suffix);

void
stopwatch_event(Stopwatch *stopwatch, const char *name);

void
stopwatch_dump(const Stopwatch *stopwatch);

#else

static inline void
stopwatch_enable(void)
{
}

static inline Stopwatch *
stopwatch_new(struct pool *pool, const char *name, const char *suffix=nullptr)
{
    (void)pool;
    (void)name;
    (void)suffix;

    return nullptr;
}

static inline Stopwatch *
stopwatch_sockaddr_new(struct pool *pool, const struct sockaddr *address,
                       size_t address_length, const char *suffix)
{
    (void)pool;
    (void)address;
    (void)address_length;
    (void)suffix;

    return nullptr;
}

static inline Stopwatch *
stopwatch_fd_new(struct pool *pool, int fd, const char *suffix)
{
    (void)pool;
    (void)fd;
    (void)suffix;

    return nullptr;
}

static inline void
stopwatch_event(Stopwatch *stopwatch, const char *name)
{
    (void)stopwatch;
    (void)name;
}

static inline void
stopwatch_dump(const Stopwatch *stopwatch)
{
    (void)stopwatch;
}

#endif

#endif
