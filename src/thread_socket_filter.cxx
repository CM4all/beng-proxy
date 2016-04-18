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
                                       ThreadSocketFilterHandler *_handler)
    :pool(_pool), queue(_queue),
     handler(_handler),
     defer_event(MakeSimpleEventCallback(ThreadSocketFilter, DeferCallback),
                 this)
{
    pool_ref(&pool);
}

ThreadSocketFilter::~ThreadSocketFilter()
{
    handler->Destroy(*this);

    defer_event.Deinit();

    encrypted_input.FreeIfDefined(fb_pool_get());
    decrypted_input.FreeIfDefined(fb_pool_get());
    plain_output.FreeIfDefined(fb_pool_get());
    encrypted_output.FreeIfDefined(fb_pool_get());

    if (error != nullptr)
        g_error_free(error);
}

void
ThreadSocketFilter::CycleBuffers()
{
    decrypted_input.FreeIfEmpty(fb_pool_get());
    encrypted_output.FreeIfEmpty(fb_pool_get());
}

void
ThreadSocketFilter::ClosedPrematurely()
{
    GError *e =
        g_error_new_literal(buffered_socket_quark(), 0,
                            "Peer closed the socket prematurely");
    socket->InvokeError(e);
}

void
ThreadSocketFilter::Destroy()
{
    DeleteUnrefPool(pool, this);
}

void
ThreadSocketFilter::Schedule()
{
    assert(!postponed_destroy);

    PreRun();

    thread_queue_add(queue, *this);
}

/**
 * @return false if the object has been destroyed
 */
bool
ThreadSocketFilter::SubmitDecryptedInput()
{
    while (true) {
        uint8_t copy[8192];
        size_t size;

        {
            const std::lock_guard<std::mutex> lock(mutex);

            auto r = decrypted_input.Read();
            if (r.IsEmpty())
                return true;

            /* copy to stack, unlock */
            size = std::min(r.size, sizeof(copy));
            memcpy(copy, r.data, size);
        }

        want_read = false;
        read_timeout = nullptr;

        switch (socket->InvokeData(copy, size)) {
        case BufferedResult::OK:
            return true;

        case BufferedResult::PARTIAL:
        case BufferedResult::BLOCKING:
            return true;

        case BufferedResult::MORE:
            expect_more = true;
            return true;

        case BufferedResult::AGAIN_OPTIONAL:
            break;

        case BufferedResult::AGAIN_EXPECT:
            expect_more = true;
            break;

        case BufferedResult::CLOSED:
            return false;
        }
    }
}

inline bool
ThreadSocketFilter::CheckRead(std::unique_lock<std::mutex> &lock)
{
    if (!want_read || encrypted_input.IsDefinedAndFull() ||
        !connected || read_scheduled)
        return true;

    read_scheduled = true;
    lock.unlock();
    socket->InternalScheduleRead(false, read_timeout);
    lock.lock();

    return true;
}

inline bool
ThreadSocketFilter::CheckWrite(std::unique_lock<std::mutex> &lock)
{
    if (!want_write || plain_output.IsDefinedAndFull())
        return true;

    lock.unlock();

    want_write = false;

    if (!socket->InvokeWrite())
        return false;

    lock.lock();
    return true;
}

void
ThreadSocketFilter::DeferCallback()
{
    std::unique_lock<std::mutex> lock(mutex);

    if (!CheckRead(lock) || !CheckWrite(lock))
        return;
}

void
ThreadSocketFilter::PreRun()
{
    const std::lock_guard<std::mutex> lock(mutex);
    decrypted_input.AllocateIfNull(fb_pool_get());
    encrypted_output.AllocateIfNull(fb_pool_get());
}

void
ThreadSocketFilter::PostRun()
{
    {
        const std::lock_guard<std::mutex> lock(mutex);
        decrypted_input.FreeIfEmpty(fb_pool_get());
        encrypted_output.FreeIfEmpty(fb_pool_get());
    }

    handler->PostRun(*this);
}

/*
 * thread_job
 *
 */

void
ThreadSocketFilter::Run()
{
    {
        const std::lock_guard<std::mutex> lock(mutex);

        if (error != nullptr)
            return;

        if (decrypted_input.IsNull() || encrypted_output.IsNull()) {
            /* caught race condition: try again, after letting
               Schedule() allocate new buffers */
            again = true;
            return;
        }

        busy = true;
    }

    GError *new_error = nullptr;
    bool success = handler->Run(*this, &new_error);

    {
        const std::lock_guard<std::mutex> lock(mutex);

        busy = false;
        done_pending = true;

        assert(error == nullptr);
        if (!success)
            error = new_error;
    }
}

void
ThreadSocketFilter::Done()
{
    if (postponed_destroy) {
        /* the object has been closed, and now that the thread has
           finished, we can finally destroy it */
        Destroy();
        return;
    }

    std::unique_lock<std::mutex> lock(mutex);

    done_pending = false;

    if (error != nullptr) {
        /* an error has occurred inside the worker thread: forward it
           to the filtered_socket */
        GError *error2 = error;
        error = nullptr;
        lock.unlock();
        socket->InvokeError(error2);
        return;
    }

    if (input_eof) {
        /* this condition was signalled by
           ThreadSocketFilterHandler::run(), probably because a TLS
           "close notify" alert was received */

        encrypted_input.FreeIfDefined(fb_pool_get());

        input_eof = false;

        lock.unlock();

        /* first flush data which was already decrypted; that is
           important because there will not be a socket event
           triggering this */
        if (!SubmitDecryptedInput())
            return;

        /* now pretend the peer has closed the connection */
        if (!socket->ClosedByPeer())
            return;

        lock.lock();
    }

    if (postponed_end && encrypted_input.IsEmpty()) {
        if (postponed_remaining) {
            if (!decrypted_input.IsEmpty()) {
                /* before we actually deliver the "remaining" event,
                   we should give the handler a chance to process the
                   data */

                lock.unlock();

                if (!SubmitDecryptedInput())
                    return;

                lock.lock();
            }

            const size_t available = decrypted_input.GetAvailable();
            lock.unlock();

            if (available == 0 && expect_more) {
                ClosedPrematurely();
                return;
            }

            postponed_remaining = false;

            if (!socket->InvokeRemaining(available))
                return;

            lock.lock();
        }

        if (decrypted_input.IsEmpty()) {
            lock.unlock();

            if (expect_more) {
                ClosedPrematurely();
                return;
            }

            socket->InvokeEnd();
            return;
        }

        lock.unlock();
        return;
    }

    if (connected) {
        // TODO: timeouts?

        if (!encrypted_input.IsDefinedAndFull())
            socket->InternalScheduleRead(expect_more, nullptr);

        if (!encrypted_output.IsEmpty())
            socket->InternalScheduleWrite();
    }

    if (!CheckWrite(lock))
        return;

    const bool drained2 = connected && drained &&
        plain_output.IsEmpty() &&
        encrypted_output.IsEmpty();

    encrypted_input.FreeIfEmpty(fb_pool_get());
    plain_output.FreeIfEmpty(fb_pool_get());

    bool _again = again;
    again = false;

    lock.unlock();

    if (drained2 && !socket->InternalDrained())
        return;

    if (!SubmitDecryptedInput())
        return;

    if (_again)
        Schedule();
    else
        PostRun();
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

    BufferedResult result;
    {
        const std::lock_guard<std::mutex> lock(f->mutex);

        f->encrypted_input.AllocateIfNull(fb_pool_get());

        auto w = f->encrypted_input.Write();
        if (w.IsEmpty())
            return BufferedResult::BLOCKING;

        result = BufferedResult::OK;
        if (length > w.size) {
            length = w.size;
            result = BufferedResult::PARTIAL;
        }

        memcpy(w.data, data, length);
        f->encrypted_input.Append(length);
    }

    f->socket->InternalConsumed(length);

    f->Schedule();

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

    return f->decrypted_input.IsDefinedAndFull();
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

    {
        const std::lock_guard<std::mutex> lock(f->mutex);

        if (!f->encrypted_input.IsEmpty() || f->decrypted_input.IsFull())
            /* just in case the filter has stalled because the
               decrypted_input buffer was full: try again */
            schedule = true;

        f->decrypted_input.Consume(nbytes);
        f->decrypted_input.FreeIfEmpty(fb_pool_get());
    }

    if (schedule)
        f->Schedule();
}

static bool
thread_socket_filter_read(bool expect_more, void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    if (expect_more)
        f->expect_more = true;

    return f->SubmitDecryptedInput() &&
        (f->postponed_end ||
         f->socket->InternalRead(false));
}

static ssize_t
thread_socket_filter_write(const void *data, size_t length, void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    ssize_t nbytes = WRITE_BLOCKING;
    bool w_empty;

    {
        const std::lock_guard<std::mutex> lock(f->mutex);

        f->plain_output.AllocateIfNull(fb_pool_get());

        auto w = f->plain_output.Write();
        w_empty = w.IsEmpty();
        if (!w_empty) {
            nbytes = std::min(length, w.size);
            memcpy(w.data, data, nbytes);
            f->plain_output.Append(nbytes);
        }
    }

    if (!w_empty) {
        f->socket->InternalUndrained();
        f->Schedule();
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

    std::unique_lock<std::mutex> lock(f->mutex);

    auto r = f->encrypted_output.Read();
    if (r.IsEmpty()) {
        lock.unlock();
        f->socket->InternalUnscheduleWrite();
        return true;
    }

    /* copy to stack, unlock */
    uint8_t copy[r.size];
    memcpy(copy, r.data, r.size);
    lock.unlock();

    ssize_t nbytes = f->socket->InternalWrite(copy, r.size);
    if (nbytes > 0) {
        lock.lock();
        const bool add = f->encrypted_output.IsFull();
        f->encrypted_output.Consume(nbytes);
        f->encrypted_output.FreeIfEmpty(fb_pool_get());
        const bool empty = f->encrypted_output.IsEmpty();
        const bool drained = empty && f->drained && f->plain_output.IsEmpty();
        lock.unlock();

        if (add)
            /* the filter job may be stalled because the output buffer
               was full; try again, now that it's not full anymore */
            f->Schedule();

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

        assert(false);
        gcc_unreachable();
    }
}

static void
thread_socket_filter_closed(void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;

    assert(f->connected);
    assert(!f->postponed_remaining);

    f->connected = false;
    f->want_write = false;
}

static bool
thread_socket_filter_remaining(size_t remaining, void *ctx)
{
    ThreadSocketFilter *f = (ThreadSocketFilter *)ctx;
    assert(!f->connected);
    assert(!f->want_write);
    assert(!f->postponed_remaining);

    if (remaining == 0) {
        std::unique_lock<std::mutex> lock(f->mutex);

        if (!f->busy && !f->done_pending && f->encrypted_input.IsEmpty()) {
            const size_t available = f->decrypted_input.GetAvailable();
            lock.unlock();

            /* forward the call */
            return f->socket->InvokeRemaining(available);
        }
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
        std::unique_lock<std::mutex> lock(f->mutex);

        if (!f->busy && !f->done_pending && f->encrypted_input.IsEmpty()) {
            const size_t available = f->decrypted_input.GetAvailable();
            lock.unlock();

            f->postponed_remaining = false;
            if (!f->socket->InvokeRemaining(available))
                return;
        } else {
            /* postpone both "remaining" and "end" */
            f->postponed_end = true;
            return;
        }
    }

    /* forward the "end" call as soon as the decrypted_input buffer
       becomes empty */

    bool empty;
    {
        const std::lock_guard<std::mutex> lock(f->mutex);
        assert(f->encrypted_input.IsEmpty());
        empty = f->decrypted_input.IsEmpty();
    }

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

    f.Destroy();
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
                         ThreadSocketFilterHandler *handler)
{
    return NewFromPool<ThreadSocketFilter>(pool,
                                           pool, queue,
                                           handler);
}
