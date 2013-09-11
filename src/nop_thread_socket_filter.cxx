/*
 * A thread_socket_filter implementation for debugging.  It performs a
 * no-op on all data.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nop_thread_socket_filter.hxx"
#include "thread_socket_filter.hxx"
#include "fifo-buffer.h"

#include <string.h>

/**
 * Copy data from #src to #dest.
 */
static void
copy(struct fifo_buffer *dest, struct fifo_buffer *src)
{
    size_t length;
    const void *s = fifo_buffer_read(src, &length);
    if (s == nullptr)
        return;

    size_t max_length;
    void *d = fifo_buffer_write(dest, &max_length);
    if (d == nullptr)
        return;

    if (length > max_length)
        length = max_length;

    memcpy(d, s, length);
    fifo_buffer_append(dest, length);
    fifo_buffer_consume(src, length);
}

/*
 * thread_socket_filter_handler
 *
 */

static bool
nop_thread_socket_filter_run(ThreadSocketFilter &f,
                             gcc_unused GError **error_r,
                             gcc_unused void *ctx)
{
    pthread_mutex_lock(&f.mutex);
    copy(f.decrypted_input, f.encrypted_input);
    copy(f.encrypted_output, f.plain_output);
    pthread_mutex_unlock(&f.mutex);
    return true;
}

static void
nop_thread_socket_filter_destroy(gcc_unused ThreadSocketFilter &f,
                                 gcc_unused void *ctx)
{
    /* nothing to do */
}

const ThreadSocketFilterHandler nop_thread_socket_filter = {
    .run = nop_thread_socket_filter_run,
    .destroy = nop_thread_socket_filter_destroy,
};

/*
 * constructor
 *
 */

void *
nop_thread_socket_filter_new(struct pool *pool)
{
    /* return a dummy pointer; we don't need it */
    return pool;
}
