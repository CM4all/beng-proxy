/*
 * A thread_socket_filter implementation for debugging.  It performs a
 * no-op on all data.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nop_thread_socket_filter.hxx"
#include "thread_socket_filter.hxx"
#include "util/ForeignFifoBuffer.hxx"

#include <string.h>

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
    f.decrypted_input.MoveFrom(f.encrypted_input);
    f.encrypted_output.MoveFrom(f.plain_output);
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
