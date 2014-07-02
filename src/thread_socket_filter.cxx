/*
 * A filtered_socket class that offloads the actual filtering to a
 * worker thread.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "thread_socket_filter.hxx"
#include "filtered_socket.hxx"
#include "fifo-buffer.h"
#include "fb_pool.h"
#include "thread_queue.hxx"

#include "gerrno.h"

#include <algorithm>

#include <string.h>
#include <errno.h>

static void
thread_socket_filter_closed_prematurely(ThreadSocketFilter *f)
{
    GError *error =
        g_error_new_literal(buffered_socket_quark(), 0,
                            "Peer closed the socket prematurely");
    filtered_socket_invoke_error(f->socket, error);
}

static void
thread_socket_filter_free_buffers(ThreadSocketFilter *f)
{
    if (f->encrypted_input != nullptr)
        fb_pool_free(f->encrypted_input);

    if (f->decrypted_input != nullptr)
        fb_pool_free(f->decrypted_input);

    if (f->plain_output != nullptr)
        fb_pool_free(f->plain_output);

    if (f->encrypted_output != nullptr)
        fb_pool_free(f->encrypted_output);

}

static void
thread_socket_filter_destroy(ThreadSocketFilter *f)
{
    f->handler->destroy(*f, f->handler_ctx);

    defer_event_deinit(&f->defer_event);

    thread_socket_filter_free_buffers(f);

    if (f->error != nullptr)
        g_error_free(f->error);

    pool_unref(f->pool);
}

static void
thread_socket_filter_schedule(ThreadSocketFilter *f)
{
    assert(!f->postponed_destroy);

    thread_queue_add(f->queue, &f->job);
}

/**
 * @return false if the object has been destroyed
 */
static bool
thread_socket_filter_submit_decrypted_input(ThreadSocketFilter *f)
{
    while (true) {
        pthread_mutex_lock(&f->mutex);

        size_t length;
        const void *data = fifo_buffer_read(f->decrypted_input, &length);
        if (data == nullptr) {
            pthread_mutex_unlock(&f->mutex);
            return true;
        }

        /* copy to stack, unlock */
        uint8_t copy[length];
        memcpy(copy, data, length);
        pthread_mutex_unlock(&f->mutex);

        f->want_read = false;
        f->read_timeout = nullptr;

        switch (filtered_socket_invoke_data(f->socket, copy, length)) {
        case BufferedResult::OK:
            return true;

        case BufferedResult::PARTIAL:
        case BufferedResult::BLOCKING:
            return true;

        case BufferedResult::MORE:
            f->expect_more = true;
            return true;

        case BufferedResult::AGAIN_OPTIONAL:
            break;

        case BufferedResult::AGAIN_EXPECT:
            f->expect_more = true;
            break;

        case BufferedResult::CLOSED:
            return false;
        }
    }
}

static bool
thread_socket_filter_check_read(ThreadSocketFilter *f)
{
    if (!f->want_read || fifo_buffer_full(f->encrypted_input) ||
        !f->connected || f->read_scheduled)
        return true;

    f->read_scheduled = true;
    pthread_mutex_unlock(&f->mutex);
    filtered_socket_internal_schedule_read(f->socket, false,
                                           f->read_timeout);
    pthread_mutex_lock(&f->mutex);

    return true;
}

static bool
thread_socket_filter_check_write(ThreadSocketFilter *f)
{
    if (!f->want_write || fifo_buffer_full(f->plain_output))
        return true;

    pthread_mutex_unlock(&f->mutex);

    f->want_write = false;

    if (!filtered_socket_invoke_write(f->socket))
        return false;

    pthread_mutex_lock(&f->mutex);
    return true;
}

static void
thread_socket_filter_defer_callback(gcc_unused int fd, gcc_unused short event,
                                    void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    pthread_mutex_lock(&f->mutex);

    if (!thread_socket_filter_check_read(f) ||
        !thread_socket_filter_check_write(f))
        return;

    pthread_mutex_unlock(&f->mutex);
}

/*
 * thread_job
 *
 */

static void
thread_socket_filter_run(struct thread_job *job)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)job;

    pthread_mutex_lock(&f->mutex);
    if (f->error != nullptr) {
        pthread_mutex_unlock(&f->mutex);
        return;
    }

    f->busy = true;
    pthread_mutex_unlock(&f->mutex);

    GError *error = nullptr;
    bool success = f->handler->run(*f, &error, f->handler_ctx);

    pthread_mutex_lock(&f->mutex);
    f->busy = false;
    f->done_pending = true;

    assert(f->error == nullptr);
    if (!success)
        f->error = error;
    pthread_mutex_unlock(&f->mutex);
}

static void
thread_socket_filter_done(struct thread_job *job)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)job;

    if (f->postponed_destroy) {
        /* the object has been closed, and now that the thread has
           finished, we can finally destroy it */
        thread_socket_filter_destroy(f);
        return;
    }

    pthread_mutex_lock(&f->mutex);

    f->done_pending = false;

    if (f->error != nullptr) {
        /* an error has occurred inside the worker thread: forward it
           to the filtered_socket */
        GError *error = f->error;
        f->error = nullptr;
        pthread_mutex_unlock(&f->mutex);
        filtered_socket_invoke_error(f->socket, error);
        return;
    }

    if (f->postponed_end && fifo_buffer_empty(f->encrypted_input)) {
        if (f->postponed_remaining) {
            if (!fifo_buffer_empty(f->decrypted_input)) {
                /* before we actually deliver the "remaining" event,
                   we should give the handler a chance to process the
                   data */

                pthread_mutex_unlock(&f->mutex);

                if (!thread_socket_filter_submit_decrypted_input(f))
                    return;

                pthread_mutex_lock(&f->mutex);
            }

            const size_t available = fifo_buffer_available(f->decrypted_input);
            pthread_mutex_unlock(&f->mutex);

            if (available == 0 && f->expect_more) {
                thread_socket_filter_closed_prematurely(f);
                return;
            }

            f->postponed_remaining = false;

            if (!filtered_socket_invoke_remaining(f->socket, available))
                return;

            pthread_mutex_lock(&f->mutex);
        }

        if (fifo_buffer_empty(f->decrypted_input)) {
            pthread_mutex_unlock(&f->mutex);

            if (f->expect_more) {
                thread_socket_filter_closed_prematurely(f);
                return;
            }

            filtered_socket_invoke_end(f->socket);
            return;
        }

        pthread_mutex_unlock(&f->mutex);
        return;
    }

    if (f->connected) {
        // TODO: timeouts?

        if (!fifo_buffer_full(f->encrypted_input))
            filtered_socket_schedule_read_no_timeout(f->socket,
                                                     f->expect_more);

        if (!fifo_buffer_empty(f->encrypted_output))
            filtered_socket_internal_schedule_write(f->socket);
    }

    if (!thread_socket_filter_check_write(f))
        return;

    const bool drained = f->connected && f->drained &&
        fifo_buffer_empty(f->plain_output) &&
        fifo_buffer_empty(f->encrypted_output);

    pthread_mutex_unlock(&f->mutex);

    if (drained && !filtered_socket_internal_drained(f->socket))
        return;

    thread_socket_filter_submit_decrypted_input(f);
}

/*
 * socket_filter
 *
 */

static void
thread_socket_filter_init_(struct filtered_socket *s, void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    f->socket = s;
}

static BufferedResult
thread_socket_filter_data(const void *data, size_t length, void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    f->read_scheduled = false;

    pthread_mutex_lock(&f->mutex);

    size_t max_length;
    void *p = fifo_buffer_write(f->encrypted_input, &max_length);
    if (p == nullptr) {
        pthread_mutex_unlock(&f->mutex);
        return BufferedResult::BLOCKING;
    }

    BufferedResult result = BufferedResult::OK;
    if (length > max_length) {
        length = max_length;
        result = BufferedResult::PARTIAL;
    }

    memcpy(p, data, length);
    fifo_buffer_append(f->encrypted_input, length);
    pthread_mutex_unlock(&f->mutex);

    filtered_socket_internal_consumed(f->socket, length);

    thread_socket_filter_schedule(f);

    return result;
}

static bool
thread_socket_filter_is_empty(void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    return f->decrypted_input == nullptr ||
        fifo_buffer_empty(f->decrypted_input);
}

static bool
thread_socket_filter_is_full(void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    return f->decrypted_input != nullptr &&
        fifo_buffer_full(f->decrypted_input);
}

static size_t
thread_socket_filter_available(void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    pthread_mutex_lock(&f->mutex);
    size_t result = fifo_buffer_available(f->decrypted_input);
    pthread_mutex_unlock(&f->mutex);
    return result;
}

static void
thread_socket_filter_consumed(size_t nbytes, void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;
    assert(f->decrypted_input != nullptr);

    bool schedule = false;

    pthread_mutex_lock(&f->mutex);

    if (!fifo_buffer_empty(f->encrypted_input) ||
        fifo_buffer_full(f->decrypted_input))
        /* just in case the filter has stalled because the
           decrypted_input buffer was full: try again */
        schedule = true;

    fifo_buffer_consume(f->decrypted_input, nbytes);

    pthread_mutex_unlock(&f->mutex);

    if (schedule)
        thread_socket_filter_schedule(f);
}

static bool
thread_socket_filter_read(bool expect_more, void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    if (expect_more)
        f->expect_more = true;

    return thread_socket_filter_submit_decrypted_input(f) &&
        (f->postponed_end ||
         filtered_socket_internal_read(f->socket, false));
}

static ssize_t
thread_socket_filter_write(const void *data, size_t length, void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    pthread_mutex_lock(&f->mutex);

    ssize_t nbytes = 0;
    size_t max_length;
    void *p = fifo_buffer_write(f->plain_output, &max_length);
    if (p != nullptr) {
        nbytes = std::min(length, max_length);
        memcpy(p, data, nbytes);
        fifo_buffer_append(f->plain_output, nbytes);
    }

    pthread_mutex_unlock(&f->mutex);

    if (nbytes > 0)
        filtered_socket_internal_undrained(f->socket);

    thread_socket_filter_schedule(f);

    return nbytes;
}

static void
thread_socket_filter_schedule_read(bool expect_more,
                                   const struct timeval *timeout,
                                   void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    if (expect_more)
        f->expect_more = true;

    f->want_read = true;
    f->read_scheduled = false;

    if (timeout != nullptr) {
        f->read_timeout_buffer = *timeout;
        timeout = &f->read_timeout_buffer;
    }

    f->read_timeout = timeout;

    defer_event_add(&f->defer_event);
}

static void
thread_socket_filter_schedule_write(void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    if (f->want_write)
        return;

    f->want_write = true;
    defer_event_add(&f->defer_event);
}

static void
thread_socket_filter_unschedule_write(void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    if (!f->want_write)
        return;

    f->want_write = false;
    defer_event_cancel(&f->defer_event);
}

static bool
thread_socket_filter_internal_write(void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    pthread_mutex_lock(&f->mutex);

    size_t length;
    const void *p = fifo_buffer_read(f->encrypted_output, &length);
    if (p == nullptr) {
        pthread_mutex_unlock(&f->mutex);
        filtered_socket_internal_unschedule_write(f->socket);
        return true;
    }

    /* copy to stack, unlock */
    uint8_t copy[length];
    memcpy(copy, p, length);
    pthread_mutex_unlock(&f->mutex);

    ssize_t nbytes = filtered_socket_internal_write(f->socket, copy, length);
    if (nbytes < 0 && errno != EAGAIN) {
        GError *error = new_error_errno_msg("write error");
        filtered_socket_invoke_error(f->socket, error);
        return false;
    }

    if (nbytes > 0) {
        pthread_mutex_lock(&f->mutex);
        const bool add = fifo_buffer_full(f->encrypted_output);
        fifo_buffer_consume(f->encrypted_output, nbytes);
        const bool empty = fifo_buffer_empty(f->encrypted_output);
        const bool drained = empty && f->drained &&
            fifo_buffer_empty(f->plain_output);
        pthread_mutex_unlock(&f->mutex);

        if (add)
            /* the filter job may be stalled because the output buffer
               was full; try again, now that it's not full anymore */
            thread_socket_filter_schedule(f);

        if (empty)
            filtered_socket_internal_unschedule_write(f->socket);

        if (drained && !filtered_socket_internal_drained(f->socket))
            return false;
    }

    return true;
}

static bool
thread_socket_filter_closed(void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    assert(f->connected);
    assert(!f->postponed_remaining);

    f->connected = false;
    f->want_write = false;

    return filtered_socket_invoke_closed(f->socket);
}

static bool
thread_socket_filter_remaining(size_t remaining, void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;
    assert(!f->connected);
    assert(!f->want_write);
    assert(!f->postponed_remaining);

    if (remaining == 0) {
        pthread_mutex_lock(&f->mutex);

        if (!f->busy && !f->done_pending &&
            fifo_buffer_empty(f->encrypted_input)) {
            const size_t available = fifo_buffer_available(f->decrypted_input);
            pthread_mutex_unlock(&f->mutex);

            /* forward the call */
            return filtered_socket_invoke_remaining(f->socket, available);
        }

        pthread_mutex_unlock(&f->mutex);
    }

    /* there's still encrypted input - postpone the remaining() call
       until we have decrypted everything */

    f->postponed_remaining = true;
    return true;
}

static void
thread_socket_filter_end(void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    assert(!f->postponed_end);

    if (f->postponed_remaining) {
        /* see if we can commit the "remaining" call now */
        pthread_mutex_lock(&f->mutex);

        if (!f->busy && !f->done_pending &&
            fifo_buffer_empty(f->encrypted_input)) {
            const size_t available = fifo_buffer_available(f->decrypted_input);
            pthread_mutex_unlock(&f->mutex);

            f->postponed_remaining = false;
            if (!filtered_socket_invoke_remaining(f->socket, available))
                return;
        } else {
            /* postpone both "remaining" and "end" */
            pthread_mutex_unlock(&f->mutex);
            f->postponed_end = true;
            return;
        }
    }

    /* forward the "end" call as soon as the decrypted_input buffer
       becomes empty */

    pthread_mutex_lock(&f->mutex);
    assert(fifo_buffer_empty(f->encrypted_input));
    const bool empty = fifo_buffer_empty(f->decrypted_input);
    pthread_mutex_unlock(&f->mutex);

    if (empty)
        /* already empty: forward the call now */
        filtered_socket_invoke_end(f->socket);
    else
        /* postpone */
        f->postponed_end = true;
}

static void
thread_socket_filter_close(void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    defer_event_cancel(&f->defer_event);

    if (!thread_queue_cancel(f->queue, &f->job)) {
        /* detach the pool, postpone the destruction */
        pool_set_persistent(f->pool);
        f->postponed_destroy = true;
        return;
    }

    thread_socket_filter_destroy(f);
}

const struct socket_filter thread_socket_filter = {
    .init = thread_socket_filter_init_,
    .data = thread_socket_filter_data,
    .is_empty = thread_socket_filter_is_empty,
    .is_full = thread_socket_filter_is_full,
    .available = thread_socket_filter_available,
    .consumed = thread_socket_filter_consumed,
    .read = thread_socket_filter_read,
    .write = thread_socket_filter_write,
    .schedule_read = thread_socket_filter_schedule_read,
    .schedule_write = thread_socket_filter_schedule_write,
    .unschedule_write = thread_socket_filter_unschedule_write,
    .internal_write = thread_socket_filter_internal_write,
    .closed = thread_socket_filter_closed,
    .remaining = thread_socket_filter_remaining,
    .end = thread_socket_filter_end,
    .close = thread_socket_filter_close,
};

/*
 * constructor
 *
 */

ThreadSocketFilter *
thread_socket_filter_new(struct pool *pool,
                         struct thread_queue *queue,
                         const ThreadSocketFilterHandler *handler,
                         void *ctx)
{
    pool_ref(pool);

    ThreadSocketFilter *f = (ThreadSocketFilter *)p_malloc(pool, sizeof(*f));
    thread_job_init(&f->job,
                    thread_socket_filter_run,
                    thread_socket_filter_done);
    f->pool = pool;
    f->queue = queue;
    f->handler = handler;
    f->handler_ctx = ctx;

    defer_event_init(&f->defer_event, thread_socket_filter_defer_callback, f);

    f->busy = false;
    f->done_pending = false;
    f->connected = true;
    f->expect_more = false;
    f->postponed_remaining = false;
    f->postponed_end = false;
    f->postponed_destroy = false;
    f->want_read = false;
    f->read_scheduled = false;
    f->want_write = false;
    f->drained = true;

    f->read_timeout = nullptr;

    pthread_mutex_init(&f->mutex, nullptr);

    f->error = nullptr;

    f->encrypted_input = fb_pool_alloc();
    f->decrypted_input = fb_pool_alloc();
    f->plain_output = fb_pool_alloc();
    f->encrypted_output = fb_pool_alloc();

    return f;
}
