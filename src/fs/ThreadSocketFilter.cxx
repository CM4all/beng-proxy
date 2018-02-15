/*
 * Copyright 2007-2018 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "ThreadSocketFilter.hxx"
#include "FilteredSocket.hxx"
#include "fb_pool.hxx"
#include "thread_queue.hxx"
#include "pool/pool.hxx"
#include "event/Duration.hxx"
#include "system/Error.hxx"

#include <algorithm>

#include <string.h>
#include <errno.h>

inline
ThreadSocketFilterInternal::~ThreadSocketFilterInternal() noexcept
{
    encrypted_input.FreeIfDefined(fb_pool_get());
    decrypted_input.FreeIfDefined(fb_pool_get());
    plain_output.FreeIfDefined(fb_pool_get());
    encrypted_output.FreeIfDefined(fb_pool_get());
}

ThreadSocketFilter::ThreadSocketFilter(EventLoop &_event_loop,
                                       ThreadQueue &_queue,
                                       ThreadSocketFilterHandler *_handler) noexcept
    :queue(_queue),
     handler(_handler),
     defer_event(_event_loop, BIND_THIS_METHOD(OnDeferred)),
     handshake_timeout_event(_event_loop,
                             BIND_THIS_METHOD(HandshakeTimeoutCallback))
{
    handshake_timeout_event.Add(EventDuration<60>::value);
}

ThreadSocketFilter::~ThreadSocketFilter() noexcept
{
    delete handler;

    defer_event.Cancel();
    handshake_timeout_event.Cancel();

    unprotected_decrypted_input.FreeIfDefined(fb_pool_get());
}

void
ThreadSocketFilter::ClosedPrematurely() noexcept
{
    socket->InvokeError(std::make_exception_ptr(std::runtime_error("Peer closed the socket prematurely")));
}

void
ThreadSocketFilter::Schedule() noexcept
{
    assert(!postponed_destroy);

    PreRun();

    thread_queue_add(queue, *this);
}

void
ThreadSocketFilter::SetHandshakeCallback(BoundMethod<void()> callback) noexcept
{
    assert(!handshake_callback);
    assert(callback);

    const std::lock_guard<std::mutex> lock(mutex);
    if (handshaking)
        /* defer */
        handshake_callback = callback;
    else
        /* handshake is already complete */
        callback();
}

inline bool
ThreadSocketFilter::MoveDecryptedInput() noexcept
{
    assert(!unprotected_decrypted_input.IsDefinedAndFull());

    const std::lock_guard<std::mutex> lock(mutex);
    const bool was_full = decrypted_input.IsDefinedAndFull();
    unprotected_decrypted_input.MoveFromAllowBothNull(decrypted_input);
    unprotected_decrypted_input.FreeIfEmpty(fb_pool_get());
    return was_full;
}

void
ThreadSocketFilter::MoveDecryptedInputAndSchedule() noexcept
{
    if (MoveDecryptedInput())
        /* just in case the filter has stalled because the
           decrypted_input buffer was full: try again */
        Schedule();
}

/**
 * @return false if the object has been destroyed
 */
bool
ThreadSocketFilter::SubmitDecryptedInput() noexcept
{
    while (true) {
        if (unprotected_decrypted_input.IsEmpty())
            MoveDecryptedInputAndSchedule();

        auto r = unprotected_decrypted_input.Read();
        if (r.empty())
            return true;

        want_read = false;
        read_timeout = nullptr;

        switch (socket->InvokeData(r.data, r.size)) {
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
ThreadSocketFilter::CheckRead(std::unique_lock<std::mutex> &lock) noexcept
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
ThreadSocketFilter::CheckWrite(std::unique_lock<std::mutex> &lock) noexcept
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
ThreadSocketFilter::OnDeferred() noexcept
{
    std::unique_lock<std::mutex> lock(mutex);

    if (!CheckRead(lock) || !CheckWrite(lock))
        return;
}

void
ThreadSocketFilter::HandshakeTimeoutCallback() noexcept
{
    bool _handshaking;

    {
        std::unique_lock<std::mutex> lock(mutex);
        _handshaking = handshaking;
    }

    if (_handshaking)
        socket->InvokeTimeout();
}

void
ThreadSocketFilter::PreRun() noexcept
{
    {
        const std::lock_guard<std::mutex> lock(mutex);
        decrypted_input.AllocateIfNull(fb_pool_get());
        encrypted_output.AllocateIfNull(fb_pool_get());
    }

    handler->PreRun(*this);
}

void
ThreadSocketFilter::PostRun() noexcept
{
    handler->PostRun(*this);

    {
        const std::lock_guard<std::mutex> lock(mutex);
        decrypted_input.FreeIfEmpty(fb_pool_get());
        encrypted_output.FreeIfEmpty(fb_pool_get());
    }
}

/*
 * thread_job
 *
 */

void
ThreadSocketFilter::Run() noexcept
{
    {
        const std::lock_guard<std::mutex> lock(mutex);

        if (error != nullptr)
            return;

        busy = true;
    }

    std::exception_ptr new_error;

    try {
        handler->Run(*this);
    } catch (...) {
        new_error = std::current_exception();
    }

    {
        const std::lock_guard<std::mutex> lock(mutex);

        busy = false;
        done_pending = true;

        assert(!error);
        error = std::move(new_error);
    }
}

void
ThreadSocketFilter::Done() noexcept
{
    if (postponed_destroy) {
        /* the object has been closed, and now that the thread has
           finished, we can finally destroy it */
        delete this;
        return;
    }

    std::unique_lock<std::mutex> lock(mutex);

    done_pending = false;

    if (error != nullptr) {
        /* an error has occurred inside the worker thread: forward it
           to the filtered_socket */

        if (socket->IsConnected()) {
            /* flush the encrypted_output buffer, because it may
               contain a "TLS alert" */
            auto r = encrypted_output.Read();
            if (!r.empty()) {
                /* don't care for the return value; the socket and
                   this object are going to be closed anyway */
                socket->InternalDirectWrite(r.data, r.size);
                socket->Shutdown();
            }
        }

        std::exception_ptr error2 = std::move(error);
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

            const size_t available = decrypted_input.GetAvailable() +
                unprotected_decrypted_input.GetAvailable();
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

        if (decrypted_input.IsEmpty() &&
            unprotected_decrypted_input.IsEmpty()) {
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

        if (!handshaking && handshake_callback) {
            auto callback = handshake_callback;
            handshake_callback = nullptr;
            callback();
        }

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

BufferedResult
ThreadSocketFilter::OnData(const void *data, size_t length) noexcept
{
    read_scheduled = false;

    BufferedResult result;
    {
        const std::lock_guard<std::mutex> lock(mutex);

        encrypted_input.AllocateIfNull(fb_pool_get());

        auto w = encrypted_input.Write();
        if (w.empty())
            return BufferedResult::BLOCKING;

        result = BufferedResult::OK;
        if (length > w.size) {
            length = w.size;
            result = BufferedResult::PARTIAL;
        }

        memcpy(w.data, data, length);
        encrypted_input.Append(length);
    }

    socket->InternalConsumed(length);

    Schedule();

    return result;
}

bool
ThreadSocketFilter::IsEmpty() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex);
    return decrypted_input.IsEmpty() &&
        unprotected_decrypted_input.IsEmpty();
}

bool
ThreadSocketFilter::IsFull() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex);
    return decrypted_input.IsDefinedAndFull() &&
        unprotected_decrypted_input.IsDefinedAndFull();
}

size_t
ThreadSocketFilter::GetAvailable() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex);
    return decrypted_input.GetAvailable() +
        unprotected_decrypted_input.GetAvailable();
}

WritableBuffer<void>
ThreadSocketFilter::ReadBuffer() noexcept
{
    if (unprotected_decrypted_input.IsEmpty())
        MoveDecryptedInputAndSchedule();

    return unprotected_decrypted_input.Read().ToVoid();
}

void
ThreadSocketFilter::Consumed(size_t nbytes) noexcept
{
    if (nbytes == 0)
        return;

    assert(unprotected_decrypted_input.IsDefined());

    unprotected_decrypted_input.Consume(nbytes);
    unprotected_decrypted_input.FreeIfEmpty(fb_pool_get());

    MoveDecryptedInputAndSchedule();
}

bool
ThreadSocketFilter::Read(bool _expect_more) noexcept
{
    if (_expect_more)
        expect_more = true;

    return SubmitDecryptedInput() &&
        (postponed_end ||
         socket->InternalRead(false));
}

inline size_t
ThreadSocketFilter::LockWritePlainOutput(const void *data, size_t size) noexcept
{
    const std::lock_guard<std::mutex> lock(mutex);

    plain_output.AllocateIfNull(fb_pool_get());

    auto w = plain_output.Write();
    size_t nbytes = std::min(size, w.size);
    memcpy(w.data, data, nbytes);
    plain_output.Append(nbytes);

    return nbytes;
}

ssize_t
ThreadSocketFilter::Write(const void *data, size_t length) noexcept
{
    // TODO: is this check necessary?
    if (length == 0)
        return 0;

    const size_t nbytes = LockWritePlainOutput(data, length);

    if (nbytes < length)
        /* set the "want_write" flag but don't schedule an event to
           avoid a busy loop; as soon as the worker thread returns, we
           will retry to write according to this flag */
        want_write = true;

    if (nbytes == 0)
        return WRITE_BLOCKING;

    socket->InternalUndrained();
    Schedule();

    return nbytes;
}

void
ThreadSocketFilter::ScheduleRead(bool _expect_more,
                                 const struct timeval *timeout) noexcept
{
    if (_expect_more)
        expect_more = true;

    want_read = true;
    read_scheduled = false;

    if (timeout != nullptr) {
        read_timeout_buffer = *timeout;
        timeout = &read_timeout_buffer;
    }

    read_timeout = timeout;

    defer_event.Schedule();
}

void
ThreadSocketFilter::ScheduleWrite() noexcept
{
    if (want_write)
        return;

    want_write = true;
    defer_event.Schedule();
}

void
ThreadSocketFilter::UnscheduleWrite() noexcept
{
    if (!want_write)
        return;

    want_write = false;

    if (!want_read)
        defer_event.Cancel();
}

bool
ThreadSocketFilter::InternalWrite() noexcept
{
    std::unique_lock<std::mutex> lock(mutex);

    auto r = encrypted_output.Read();
    if (r.empty()) {
        lock.unlock();
        socket->InternalUnscheduleWrite();
        return true;
    }

    /* copy to stack, unlock */
    uint8_t copy[r.size];
    memcpy(copy, r.data, r.size);
    lock.unlock();

    ssize_t nbytes = socket->InternalWrite(copy, r.size);
    if (nbytes > 0) {
        lock.lock();
        const bool add = encrypted_output.IsFull();
        encrypted_output.Consume(nbytes);
        encrypted_output.FreeIfEmpty(fb_pool_get());
        const bool empty = encrypted_output.IsEmpty();
        const bool _drained = empty && drained && plain_output.IsEmpty();
        lock.unlock();

        if (add)
            /* the filter job may be stalled because the output buffer
               was full; try again, now that it's not full anymore */
            Schedule();

        if (empty)
            socket->InternalUnscheduleWrite();

        if (_drained && !socket->InternalDrained())
            return false;

        return true;
    } else {
        switch ((enum write_result)nbytes) {
        case WRITE_SOURCE_EOF:
            assert(false);
            gcc_unreachable();

        case WRITE_ERRNO:
            socket->InvokeError(std::make_exception_ptr(MakeErrno("write error")));
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

void
ThreadSocketFilter::OnClosed() noexcept
{
    assert(connected);
    assert(!postponed_remaining);

    connected = false;
    want_write = false;

    handshake_timeout_event.Cancel();
}

bool
ThreadSocketFilter::OnRemaining(size_t remaining) noexcept
{
    assert(!connected);
    assert(!want_write);
    assert(!postponed_remaining);

    if (remaining == 0) {
        std::unique_lock<std::mutex> lock(mutex);

        if (!busy && !done_pending && encrypted_input.IsEmpty()) {
            const size_t available = decrypted_input.GetAvailable() +
                unprotected_decrypted_input.GetAvailable();
            lock.unlock();

            /* forward the call */
            return socket->InvokeRemaining(available);
        }
    }

    /* there's still encrypted input - postpone the remaining() call
       until we have decrypted everything */

    postponed_remaining = true;
    return true;
}

void
ThreadSocketFilter::OnEnd() noexcept
{
    assert(!postponed_end);

    if (postponed_remaining) {
        /* see if we can commit the "remaining" call now */
        std::unique_lock<std::mutex> lock(mutex);

        if (!busy && !done_pending && encrypted_input.IsEmpty()) {
            const size_t available = decrypted_input.GetAvailable() +
                unprotected_decrypted_input.GetAvailable();
            lock.unlock();

            postponed_remaining = false;
            if (!socket->InvokeRemaining(available))
                return;
        } else {
            /* postpone both "remaining" and "end" */
            postponed_end = true;
            return;
        }
    }

    /* forward the "end" call as soon as the decrypted_input buffer
       becomes empty */

    bool empty;
    {
        const std::lock_guard<std::mutex> lock(mutex);
        assert(encrypted_input.IsEmpty());
        empty = decrypted_input.IsEmpty() &&
            unprotected_decrypted_input.IsEmpty();
    }

    if (empty)
        /* already empty: forward the call now */
        socket->InvokeEnd();
    else
        /* postpone */
        postponed_end = true;
}

void
ThreadSocketFilter::Close() noexcept
{
    defer_event.Cancel();

    if (!thread_queue_cancel(queue, *this)) {
        /* postpone the destruction */
        postponed_destroy = true;
        return;
    }

    delete this;
}
