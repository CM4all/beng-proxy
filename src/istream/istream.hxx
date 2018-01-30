/*
 * Copyright 2007-2017 Content Management AG
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

#ifndef ISTREAM_HXX
#define ISTREAM_HXX

#include "pool/pool.hxx"
#include "Handler.hxx"
#include "util/LeakDetector.hxx"

#include <algorithm>

#include <assert.h>

class IstreamBucketList;

/**
 * An asynchronous input stream.
 *
 * The lifetime of an #istream begins when it is created, and ends
 * with one of the following events:
 *
 * - it is closed manually using istream_close()
 * - it is invalidated by a successful istream_as_fd() call
 * - it has reached end-of-file
 * - an error has occurred
 */
class Istream : PoolHolder, LeakDetector {
    /** data sink */
    IstreamHandler *handler = nullptr;

    /** which types of file descriptors are accepted by the handler? */
    FdTypeMask handler_direct = 0;

#ifndef NDEBUG
    bool reading = false, destroyed = false;

    bool closing = false, eof = false;

    bool in_data = false, available_full_set = false;

    /** how much data was available in the previous invocation? */
    size_t data_available = 0;

    off_t available_partial = 0, available_full = 0;
#endif

protected:
    explicit Istream(struct pool &_pool) noexcept
        :PoolHolder(_pool) {}

    Istream(const Istream &) = delete;
    Istream &operator=(const Istream &) = delete;

    virtual ~Istream() noexcept;

    using PoolHolder::GetPool;

public:
    FdTypeMask GetHandlerDirect() const {
        return handler_direct;
    }

protected:
    bool CheckDirect(FdType type) const {
        return (handler_direct & FdTypeMask(type)) != 0;
    }

    void Consumed(size_t nbytes) {
#ifdef NDEBUG
        (void)nbytes;
#else
        if ((off_t)nbytes >= available_partial)
            available_partial = 0;
        else
            available_partial -= nbytes;

        if (available_full_set) {
            assert((off_t)nbytes <= available_full);

            available_full -= (off_t)nbytes;
        }

        data_available -= std::min(nbytes, data_available);
#endif
    }

    size_t InvokeData(const void *data, size_t length) noexcept;
    ssize_t InvokeDirect(FdType type, int fd, size_t max_length) noexcept;
    void InvokeEof() noexcept;
    void InvokeError(std::exception_ptr ep) noexcept;

    void Destroy() noexcept {
        this->~Istream();
        /* no need to free memory from the pool */
    }

    void DestroyEof() noexcept {
        InvokeEof();
        Destroy();
    }

    void DestroyError(std::exception_ptr ep) noexcept {
        InvokeError(ep);
        Destroy();
    }

    /**
     * @return the number of bytes still in the buffer
     */
    template<typename Buffer>
    size_t ConsumeFromBuffer(Buffer &buffer) noexcept {
        auto r = buffer.Read().ToVoid();
        if (r.empty())
            return 0;

        size_t consumed = InvokeData(r.data, r.size);
        if (consumed > 0)
            buffer.Consume(consumed);
        return r.size - consumed;
    }

    /**
     * @return the number of bytes consumed
     */
    template<typename Buffer>
    size_t SendFromBuffer(Buffer &buffer) noexcept {
        auto r = buffer.Read().ToVoid();
        if (r.empty())
            return 0;

        size_t consumed = InvokeData(r.data, r.size);
        if (consumed > 0)
            buffer.Consume(consumed);
        return consumed;
    }

public:
    bool HasHandler() const noexcept {
        assert(!destroyed);

        return handler != nullptr;
    }

    void SetHandler(IstreamHandler &_handler,
                    FdTypeMask _handler_direct=0) noexcept {
        assert(!destroyed);

        handler = &_handler;
        handler_direct = _handler_direct;
    }

    void SetDirect(FdTypeMask _handler_direct) noexcept {
        assert(!destroyed);

        handler_direct = _handler_direct;
    }

    /**
     * How much data is available?
     *
     * @param partial if false, the stream must provide the data size
     * until the end of the stream; for partial, a minimum estimate is
     * ok
     * @return the number of bytes available or -1 if the object does
     * not know
     */
    gcc_pure
    off_t GetAvailable(bool partial) noexcept {
#ifndef NDEBUG
        assert(!destroyed);
        assert(!closing);
        assert(!eof);
        assert(!reading);

        struct pool_notify_state notify;
        pool_notify(pool, &notify);
        reading = true;
#endif

        off_t available = _GetAvailable(partial);

#ifndef NDEBUG
        assert(available >= -1);
        assert(!pool_denotify(&notify));
        assert(!destroyed);
        assert(reading);

        reading = false;

        if (partial) {
            assert(available_partial == 0 ||
                   available >= available_partial);
            if (available > available_partial)
                available_partial = available;
        } else {
            assert(!available_full_set ||
                   available_full == available);
            if (!available_full_set && available != (off_t)-1) {
                available_full = available;
                available_full_set = true;
            }
        }
#endif

        return available;
    }

    /**
     * Skip data without processing it.  By skipping 0 bytes, you can
     * test whether the stream is able to skip at all.
     *
     * @return the number of bytes skipped or -1 if skipping is not supported
     */
    off_t Skip(off_t length) noexcept {
#ifndef NDEBUG
        assert(!destroyed);
        assert(!closing);
        assert(!eof);
        assert(!reading);

        struct pool_notify_state notify;
        pool_notify(pool, &notify);
        reading = true;
#endif

        off_t nbytes = _Skip(length);
        assert(nbytes <= length);

#ifndef NDEBUG
        if (pool_denotify(&notify) || destroyed)
            return nbytes;

        reading = false;

        if (nbytes > 0) {
            if (nbytes > available_partial)
                available_partial = 0;
            else
                available_partial -= nbytes;

            assert(!available_full_set ||
                   nbytes < available_full);
            if (available_full_set)
                available_full -= nbytes;
        }
#endif

        return nbytes;
    }

    /**
     * Try to read from the stream.  If the stream can read data
     * without blocking, it must provide data.  It may invoke the
     * callbacks any number of times, supposed that the handler itself
     * doesn't block.
     *
     * If the stream does not provide data immediately (and it is not
     * at EOF yet), it must install an event and invoke the handler
     * later, whenever data becomes available.
     *
     * Whenever the handler reports it is blocking, the responsibility
     * for calling back (and calling this function) is handed back to
     * the istream handler.
     */
    void Read() noexcept  {
#ifndef NDEBUG
        assert(!destroyed);
        assert(!closing);
        assert(!eof);
        assert(!reading);
        assert(!in_data);

        struct pool_notify_state notify;
        pool_notify(pool, &notify);
        reading = true;
#endif

        _Read();

#ifndef NDEBUG
        if (pool_denotify(&notify) || destroyed)
            return;

        reading = false;
#endif
    }

    /**
     * Append #IstreamBucket instances with consecutive data from this
     * #Istream to the end of the given #IstreamBucketList.  Unless
     * the returned data marks the end of the stream,
     * IstreamBucketList::SetMore() must be called.
     *
     * On error, this method destroys the #Istream instance and throws
     * std::runtime_error.
     */
    void FillBucketList(IstreamBucketList &list) {
#ifndef NDEBUG
        assert(!destroyed);
        assert(!closing);
        assert(!eof);
        assert(!reading);
        assert(!in_data);

        struct pool_notify_state notify;
        pool_notify(pool, &notify);
        reading = true;

        try {
#endif

            _FillBucketList(list);

#ifndef NDEBUG
        } catch (...) {
            if (!pool_denotify(&notify)) {
                assert(destroyed);
            }

            throw;
        }

        assert(!pool_denotify(&notify));
        assert(!destroyed);
        assert(reading);

        reading = false;

#if 0
        // TODO: not possible currently due to include dependencies
        size_t total_size = list.GetTotalBufferSize();
        if ((off_t)total_size > available_partial)
            available_partial = total_size;

        if (!list.HasMore() && !list.HasNonBuffer()) {
            if (available_full_set)
                assert((off_t)total_size == available_full);
            else
                available_full = total_size;
        }
#endif
#endif
    }

    /**
     * Consume data from the #IstreamBucketList filled by
     * FillBucketList().
     *
     * @param nbytes the number of bytes to be consumed; may be more
     * than returned by FillBucketList(), because some of the data may
     * be returned by this Istream's successive siblings
     *
     * @return the number of bytes really consumed by this instance
     * (the rest will be consumed by its siblings)
     */
    size_t ConsumeBucketList(size_t nbytes) noexcept {
#ifndef NDEBUG
        assert(!destroyed);
        assert(!closing);
        assert(!eof);
        assert(!reading);
        assert(!in_data);
#endif

        auto result = _ConsumeBucketList(nbytes);

#ifndef NDEBUG
        assert(!destroyed);
        assert(result <= nbytes);
#endif

        return result;
    }

    /**
     * Close the istream object, and return the remaining data as a
     * file descriptor.  This fd can be read until end-of-stream.
     * Returns -1 if this is not possible (the stream object is still
     * usable).
     */
    int AsFd() noexcept {
#ifndef NDEBUG
        assert(!destroyed);
        assert(!closing);
        assert(!eof);
        assert(!reading);
        assert(!in_data);

        struct pool_notify_state notify;
        pool_notify(pool, &notify);
        reading = true;
#endif

        int fd = _AsFd();

#ifndef NDEBUG
        assert(!pool_denotify(&notify) || fd < 0);

        if (fd < 0)
            reading = false;
#endif

        return fd;
    }

    /**
     * Close the stream and free resources.  This must not be called
     * after the handler's eof() / abort() callbacks were invoked.
     */
    void Close() noexcept {
#ifndef NDEBUG
        assert(!destroyed);
        assert(!closing);
        assert(!eof);

        closing = true;
#endif

        _Close();
    }

    /**
     * Close an istream which was never used, i.e. it does not have a
     * handler yet.
     */
    void CloseUnused() noexcept {
        assert(!HasHandler());

        Close();
    }

protected:
    virtual off_t _GetAvailable(gcc_unused bool partial) noexcept {
        return -1;
    }

    virtual off_t _Skip(gcc_unused off_t length) noexcept {
        return -1;
    }

    virtual void _Read() noexcept = 0;

    virtual void _FillBucketList(IstreamBucketList &list);
    virtual size_t _ConsumeBucketList(size_t nbytes) noexcept;

    virtual int _AsFd() noexcept {
        return -1;
    }

    virtual void _Close() noexcept {
        Destroy();
    }
};

#include "Invoke.hxx"

template<typename T, typename... Args>
static inline T *
NewIstream(struct pool &pool, Args&&... args)
{
    return NewFromPool<T>(pool, pool,
                          std::forward<Args>(args)...);
}

#endif
