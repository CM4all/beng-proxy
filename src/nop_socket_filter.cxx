/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nop_socket_filter.hxx"
#include "filtered_socket.hxx"

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
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    f->socket = s;
}

static BufferedResult
nop_socket_filter_data(const void *data, size_t length, void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return filtered_socket_invoke_data(f->socket, data, length);
}

static bool
nop_socket_filter_is_empty(void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return filtered_socket_internal_is_empty(f->socket);
}

static bool
nop_socket_filter_is_full(void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return filtered_socket_internal_is_full(f->socket);
}

static size_t
nop_socket_filter_available(void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return filtered_socket_internal_available(f->socket);
}

static void
nop_socket_filter_consumed(size_t nbytes, void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    filtered_socket_internal_consumed(f->socket, nbytes);
}

static bool
nop_socket_filter_read(bool expect_more, void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return filtered_socket_internal_read(f->socket, expect_more);
}

static ssize_t
nop_socket_filter_write(const void *data, size_t length, void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return filtered_socket_internal_write(f->socket, data, length);
}

static bool
nop_socket_filter_internal_write(void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return filtered_socket_invoke_write(f->socket);
}

static bool
nop_socket_filter_closed(void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return filtered_socket_invoke_closed(f->socket);
}

static bool
nop_socket_filter_remaining(size_t remaining, void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return filtered_socket_invoke_remaining(f->socket, remaining);
}

static void
nop_socket_filter_end(void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    filtered_socket_invoke_end(f->socket);
}

static void
nop_socket_filter_close(void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

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
    .remaining = nop_socket_filter_remaining,
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
    return NewFromPool<struct nop_socket_filter>(pool);
}
