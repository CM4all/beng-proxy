/*
 * A filtered_socket class that offloads the actual filtering to a
 * worker thread.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "thread_socket_filter.hxx"
#include "filtered_socket.hxx"
#include "fb_pool.hxx"
#include "thread_queue.hxx"
#include "pool.hxx"
#include "event/Callback.hxx"

#include "gerrno.h"

#include <algorithm>

#include <string.h>
#include <errno.h>

inline
ThreadSocketFilter::ThreadSocketFilter(struct pool &_pool,
                                       ThreadQueue &_queue,
                                       const ThreadSocketFilterHandler &_handler,
                                       void *_ctx)
    :pool(_pool), queue(_queue),
     handler(_handler), handler_ctx(_ctx),
     defer_event(MakeSimpleEventCallback(ThreadSocketFilter, DeferCallback),
                 this),
     decrypted_input(fb_pool_get()),
     encrypted_output(fb_pool_get())
{
    pool_ref(&pool);
}

ThreadSocketFilter::~ThreadSocketFilter()
{
    handler.destroy(*this, handler_ctx);

    defer_event.Deinit();

    encrypted_input.FreeIfDefined(fb_pool_get());
    decrypted_input.Free(fb_pool_get());
    plain_output.FreeIfDefined(fb_pool_get());
    encrypted_output.Free(fb_pool_get());

    if (error != nullptr)
        g_error_free(error);
}

static void
thread_socket_filter_closed_prematurely(ThreadSocketFilter *f)
{
    GError *error =
        g_error_new_literal(buffered_socket_quark(), 0,
                            "Peer closed the socket prematurely");
    f->socket->InvokeError(error);
}

static void
thread_socket_filter_destroy(ThreadSocketFilter *f)
{
    DeleteUnrefPool(f->pool, f);
}

static void
thread_socket_filter_schedule(ThreadSocketFilter &f)
{
    assert(!f.postponed_destroy);

    thread_queue_add(f.queue, f);
}

/**
 * @return false if the object has been destroyed
 */
static bool
thread_socket_filter_submit_decrypted_input(ThreadSocketFilter *f)
{
    while (true) {
        f->mutex.lock();

        auto r = f->decrypted_input.Read();
        if (r.IsEmpty()) {
            f->mutex.unlock();
            return true;
        }

        /* copy to stack, unlock */
        uint8_t copy[r.size];
        memcpy(copy, r.data, r.size);
        f->mutex.unlock();

        f->want_read = false;
        f->read_timeout = nullptr;

        switch (f->socket->InvokeData(copy, r.size)) {
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
    if (!f->want_read || f->encrypted_input.IsDefinedAndFull() ||
        !f->connected || f->read_scheduled)
        return true;

    f->read_scheduled = true;
    f->mutex.unlock();
    f->socket->InternalScheduleRead(false, f->read_timeout);
    f->mutex.lock();

    return true;
}

static bool
thread_socket_filter_check_write(ThreadSocketFilter *f)
{
    if (!f->want_write || f->plain_output.IsDefinedAndFull())
        return true;

    f->mutex.unlock();

    f->want_write = false;

    if (!f->socket->InvokeWrite())
        return false;

    f->mutex.lock();
    return true;
}

void
ThreadSocketFilter::DeferCallback()
{
    mutex.lock();

    if (!thread_socket_filter_check_read(this) ||
        !thread_socket_filter_check_write(this))
        return;

    mutex.unlock();
}

/*
 * thread_job
 *
 */

void
ThreadSocketFilter::Run()
{
    mutex.lock();
    if (error != nullptr) {
        mutex.unlock();
        return;
    }

    busy = true;
    mutex.unlock();

    GError *new_error = nullptr;
    bool success = handler.run(*this, &new_error, handler_ctx);

    mutex.lock();
    busy = false;
    done_pending = true;

    assert(error == nullptr);
    if (!success)
        error = new_error;
    mutex.unlock();
}

void
ThreadSocketFilter::Done()
{
    if (postponed_destroy) {
        /* the object has been closed, and now that the thread has
           finished, we can finally destroy it */
        thread_socket_filter_destroy(this);
        return;
    }

    mutex.lock();

    done_pending = false;

    if (error != nullptr) {
        /* an error has occurred inside the worker thread: forward it
           to the filtered_socket */
        GError *error2 = error;
        error = nullptr;
        mutex.unlock();
        socket->InvokeError(error2);
        return;
    }

    if (postponed_end && encrypted_input.IsEmpty()) {
        if (postponed_remaining) {
            if (!decrypted_input.IsEmpty()) {
                /* before we actually deliver the "remaining" event,
                   we should give the handler a chance to process the
                   data */

                mutex.unlock();

                if (!thread_socket_filter_submit_decrypted_input(this))
                    return;

                mutex.lock();
            }

            const size_t available = decrypted_input.GetAvailable();
            mutex.unlock();

            if (available == 0 && expect_more) {
                thread_socket_filter_closed_prematurely(this);
                return;
            }

            postponed_remaining = false;

            if (!socket->InvokeRemaining(available))
                return;

            mutex.lock();
        }

        if (decrypted_input.IsEmpty()) {
            mutex.unlock();

            if (expect_more) {
                thread_socket_filter_closed_prematurely(this);
                return;
            }

            socket->InvokeEnd();
            return;
        }

        mutex.unlock();
        return;
    }

    if (connected) {
        // TODO: timeouts?

        if (!encrypted_input.IsDefinedAndFull())
            socket->InternalScheduleRead(expect_more, nullptr);

        if (!encrypted_output.IsEmpty())
            socket->InternalScheduleWrite();
    }

    if (!thread_socket_filter_check_write(this))
        return;

    const bool drained2 = connected && drained &&
        plain_output.IsEmpty() &&
        encrypted_output.IsEmpty();

    encrypted_input.FreeIfEmpty(fb_pool_get());
    plain_output.FreeIfEmpty(fb_pool_get());

    mutex.unlock();

    if (drained2 && !socket->InternalDrained())
        return;

    thread_socket_filter_submit_decrypted_input(this);
}

/*
 * socket_filter
 *
 */

static void
thread_socket_filter_init_(FilteredSocket &s, void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    f->socket = &s;
}

static BufferedResult
thread_socket_filter_data(const void *data, size_t length, void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    f->read_scheduled = false;

    f->mutex.lock();

    f->encrypted_input.AllocateIfNull(fb_pool_get());

    auto w = f->encrypted_input.Write();
    if (w.IsEmpty()) {
        f->mutex.unlock();
        return BufferedResult::BLOCKING;
    }

    BufferedResult result = BufferedResult::OK;
    if (length > w.size) {
        length = w.size;
        result = BufferedResult::PARTIAL;
    }

    memcpy(w.data, data, length);
    f->encrypted_input.Append(length);
    f->mutex.unlock();

    f->socket->InternalConsumed(length);

    thread_socket_filter_schedule(*f);

    return result;
}

static bool
thread_socket_filter_is_empty(void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    return f->decrypted_input.IsEmpty();
}

static bool
thread_socket_filter_is_full(void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    return f->decrypted_input.IsFull();
}

static size_t
thread_socket_filter_available(void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    std::lock_guard<std::mutex> lock(f->mutex);
    return f->decrypted_input.GetAvailable();
}

static void
thread_socket_filter_consumed(size_t nbytes, void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;
    assert(f->decrypted_input.IsDefined());

    bool schedule = false;

    f->mutex.lock();

    if (!f->encrypted_input.IsEmpty() || f->decrypted_input.IsFull())
        /* just in case the filter has stalled because the
           decrypted_input buffer was full: try again */
        schedule = true;

    f->decrypted_input.Consume(nbytes);

    f->mutex.unlock();

    if (schedule)
        thread_socket_filter_schedule(*f);
}

static bool
thread_socket_filter_read(bool expect_more, void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    if (expect_more)
        f->expect_more = true;

    return thread_socket_filter_submit_decrypted_input(f) &&
        (f->postponed_end ||
         f->socket->InternalRead(false));
}

static ssize_t
thread_socket_filter_write(const void *data, size_t length, void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    f->mutex.lock();

    ssize_t nbytes = WRITE_BLOCKING;

    f->plain_output.AllocateIfNull(fb_pool_get());

    auto w = f->plain_output.Write();
    if (!w.IsEmpty()) {
        nbytes = std::min(length, w.size);
        memcpy(w.data, data, nbytes);
        f->plain_output.Append(nbytes);
    }

    f->mutex.unlock();

    if (!w.IsEmpty()) {
        f->socket->InternalUndrained();
        thread_socket_filter_schedule(*f);
    }

    if (nbytes == WRITE_BLOCKING)
        /* set the "want_write" flag but don't schedule an event to
           avoid a busy loop; as soon as the worker thread returns, we
           will retry to write according to this flag */
        f->want_write = true;

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

    f->defer_event.Add();
}

static void
thread_socket_filter_schedule_write(void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    if (f->want_write)
        return;

    f->want_write = true;
    f->defer_event.Add();
}

static void
thread_socket_filter_unschedule_write(void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    if (!f->want_write)
        return;

    f->want_write = false;

    if (!f->want_read)
        f->defer_event.Cancel();
}

static bool
thread_socket_filter_internal_write(void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    f->mutex.lock();

    auto r = f->encrypted_output.Read();
    if (r.IsEmpty()) {
        f->mutex.unlock();
        f->socket->InternalUnscheduleWrite();
        return true;
    }

    /* copy to stack, unlock */
    uint8_t copy[r.size];
    memcpy(copy, r.data, r.size);
    f->mutex.unlock();

    ssize_t nbytes = f->socket->InternalWrite(copy, r.size);
    if (nbytes > 0) {
        f->mutex.lock();
        const bool add = f->encrypted_output.IsFull();
        f->encrypted_output.Consume(nbytes);
        const bool empty = f->encrypted_output.IsEmpty();
        const bool drained = empty && f->drained && f->plain_output.IsEmpty();
        f->mutex.unlock();

        if (add)
            /* the filter job may be stalled because the output buffer
               was full; try again, now that it's not full anymore */
            thread_socket_filter_schedule(*f);

        if (empty)
            f->socket->InternalUnscheduleWrite();

        if (drained && !f->socket->InternalDrained())
            return false;

        return true;
    } else {
        switch ((enum write_result)nbytes) {
        case WRITE_SOURCE_EOF:
            assert(false);
            gcc_unreachable();

        case WRITE_ERRNO:
            if (errno == EAGAIN)
                return true;

            f->socket->InvokeError(new_error_errno_msg("write error"));
            return false;

        case WRITE_BLOCKING:
            return true;

        case WRITE_DESTROYED:
            return false;

        case WRITE_BROKEN:
            return true;
        }
    }
}

static bool
thread_socket_filter_closed(void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    assert(f->connected);
    assert(!f->postponed_remaining);

    f->connected = false;
    f->want_write = false;

    return f->socket->InvokeClosed();
}

static bool
thread_socket_filter_remaining(size_t remaining, void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;
    assert(!f->connected);
    assert(!f->want_write);
    assert(!f->postponed_remaining);

    if (remaining == 0) {
        f->mutex.lock();

        if (!f->busy && !f->done_pending && f->encrypted_input.IsEmpty()) {
            const size_t available = f->decrypted_input.GetAvailable();
            f->mutex.unlock();

            /* forward the call */
            return f->socket->InvokeRemaining(available);
        }

        f->mutex.unlock();
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
        f->mutex.lock();

        if (!f->busy && !f->done_pending && f->encrypted_input.IsEmpty()) {
            const size_t available = f->decrypted_input.GetAvailable();
            f->mutex.unlock();

            f->postponed_remaining = false;
            if (!f->socket->InvokeRemaining(available))
                return;
        } else {
            /* postpone both "remaining" and "end" */
            f->mutex.unlock();
            f->postponed_end = true;
            return;
        }
    }

    /* forward the "end" call as soon as the decrypted_input buffer
       becomes empty */

    f->mutex.lock();
    assert(f->encrypted_input.IsEmpty());
    const bool empty = f->decrypted_input.IsEmpty();
    f->mutex.unlock();

    if (empty)
        /* already empty: forward the call now */
        f->socket->InvokeEnd();
    else
        /* postpone */
        f->postponed_end = true;
}

static void
thread_socket_filter_close(void *ctx)
{
    auto &f = *(ThreadSocketFilter *)ctx;

    f.defer_event.Cancel();

    if (!thread_queue_cancel(f.queue, f)) {
        /* detach the pool, postpone the destruction */
        pool_set_persistent(&f.pool);
        f.postponed_destroy = true;
        return;
    }

    thread_socket_filter_destroy(&f);
}

const SocketFilter thread_socket_filter = {
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
thread_socket_filter_new(struct pool &pool,
                         ThreadQueue &queue,
                         const ThreadSocketFilterHandler &handler,
                         void *ctx)
{
    return NewFromPool<ThreadSocketFilter>(pool,
                                           pool, queue,
                                           handler, ctx);
}
