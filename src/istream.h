/*
 * Asynchronous input stream API.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ISTREAM_H
#define __BENG_ISTREAM_H

#include <sys/types.h>

typedef struct istream *istream_t;

struct istream_handler {
    size_t (*data)(const void *data, size_t length, void *ctx);
    ssize_t (*direct)(int fd, size_t max_length, void *ctx);
    void (*eof)(void *ctx);
    void (*free)(void *ctx);
};

struct istream {
    pool_t pool;
    const struct istream_handler *handler;
    void *handler_ctx;

    void (*read)(istream_t istream);
    void (*direct)(istream_t istream);
    void (*close)(istream_t istream);
};

static inline void
istream_read(istream_t istream)
{
    istream->read(istream);
}

static inline void
istream_direct(istream_t istream)
{
    if (istream->direct == NULL)
        istream->read(istream);
    else
        istream->direct(istream);
}

static inline void
istream_close(istream_t istream)
{
    istream->close(istream);
}

#endif
