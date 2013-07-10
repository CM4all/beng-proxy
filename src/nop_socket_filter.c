/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nop_socket_filter.h"
#include "filtered_socket.h"
#include "fifo-buffer.h"
#include "fb_pool.h"
#include "thread_queue.h"
#include "thread_pool.h"
#include "gerrno.h"

#include <string.h>
#include <errno.h>

struct nop_socket_filter {
    struct filtered_socket *socket;
};

/*
 * socket_filter
 *
 */

static void
nop_socket_filter_init(struct filtered_socket *s, void *ctx)
{
    struct nop_socket_filter *f = ctx;

    f->socket = s;
}

static enum buffered_result
nop_socket_filter_data(const void *data, size_t length, void *ctx)
{
    struct nop_socket_filter *f = ctx;

    return filtered_socket_invoke_data(f->socket, data, length);
}

static bool
nop_socket_filter_is_empty(void *ctx)
{
    struct nop_socket_filter *f = ctx;

    return filtered_socket_internal_is_empty(f->socket);
}

static bool
nop_socket_filter_is_full(void *ctx)
{
    struct nop_socket_filter *f = ctx;

    return filtered_socket_internal_is_full(f->socket);
}

static size_t
nop_socket_filter_available(void *ctx)
{
    struct nop_socket_filter *f = ctx;

    return filtered_socket_internal_available(f->socket);
}

static void
nop_socket_filter_consumed(size_t nbytes, void *ctx)
{
    struct nop_socket_filter *f = ctx;

    filtered_socket_internal_consumed(f->socket, nbytes);
}

static bool
nop_socket_filter_read(void *ctx)
{
    struct nop_socket_filter *f = ctx;

    return filtered_socket_internal_read(f->socket);
}

static ssize_t
nop_socket_filter_write(const void *data, size_t length, void *ctx)
{
    struct nop_socket_filter *f = ctx;

    return filtered_socket_internal_write(f->socket, data, length);
}

static bool
nop_socket_filter_internal_write(void *ctx)
{
    struct nop_socket_filter *f = ctx;

    return filtered_socket_invoke_write(f->socket);
}

static bool
nop_socket_filter_closed(size_t remaining, void *ctx)
{
    struct nop_socket_filter *f = ctx;

    return filtered_socket_invoke_closed(f->socket, remaining);
}

static void
nop_socket_filter_end(void *ctx)
{
    struct nop_socket_filter *f = ctx;

    filtered_socket_invoke_end(f->socket);
}

static void
nop_socket_filter_close(void *ctx)
{
    struct nop_socket_filter *f = ctx;

    (void)f;
}

const struct socket_filter nop_socket_filter = {
    .init = nop_socket_filter_init,
    .data = nop_socket_filter_data,
    .is_empty = nop_socket_filter_is_empty,
    .is_full = nop_socket_filter_is_full,
    .available = nop_socket_filter_available,
    .consumed = nop_socket_filter_consumed,
    .read = nop_socket_filter_read,
    .write = nop_socket_filter_write,
    .internal_write = nop_socket_filter_internal_write,
    .closed = nop_socket_filter_closed,
    .end = nop_socket_filter_end,
    .close = nop_socket_filter_close,
};

/*
 * constructor
 *
 */

void *
nop_socket_filter_new(struct pool *pool)
{
    struct nop_socket_filter *n = p_malloc(pool, sizeof(*n));
    return n;
}
