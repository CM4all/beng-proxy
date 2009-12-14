/*
 * Statistics collector.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STOPWATCH_H
#define BENG_PROXY_STOPWATCH_H

#include "pool.h"

struct stopwatch;
struct sockaddr;

#ifdef ENABLE_STOPWATCH

void
stopwatch_enable(void);

struct stopwatch *
stopwatch_new(pool_t pool, const char *name);

struct stopwatch *
stopwatch_sockaddr_new(pool_t pool, const struct sockaddr *address,
                       size_t address_length, const char *suffix);

struct stopwatch *
stopwatch_fd_new(pool_t pool, int fd, const char *suffix);

void
stopwatch_event(struct stopwatch *stopwatch, const char *name);

void
stopwatch_dump(const struct stopwatch *stopwatch);

#else

static inline void
stopwatch_enable(void)
{
}

static inline struct stopwatch *
stopwatch_new(pool_t pool, const char *name)
{
    (void)pool;
    (void)name;

    return NULL;
}

static inline struct stopwatch *
stopwatch_sockaddr_new(pool_t pool, const struct sockaddr *address,
                       size_t address_length, const char *suffix)
{
    (void)pool;
    (void)address;
    (void)address_length;
    (void)suffix;

    return NULL;
}

static inline struct stopwatch *
stopwatch_fd_new(pool_t pool, int fd, const char *suffix)
{
    (void)pool;
    (void)fd;
    (void)suffix;

    return NULL;
}

static inline void
stopwatch_event(struct stopwatch *stopwatch, const char *name)
{
    (void)stopwatch;
    (void)name;
}

static inline void
stopwatch_dump(const struct stopwatch *stopwatch)
{
    (void)stopwatch;
}

#endif

#endif
