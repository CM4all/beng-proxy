/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ISTREAM_OO_HXX
#define ISTREAM_OO_HXX

#include "pool.hxx"
#include "Struct.hxx"
#include "Handler.hxx"

class Istream : istream {
protected:
    explicit Istream(struct pool &_pool)
        :istream(_pool) {}

    Istream(const Istream &) = delete;
    Istream &operator=(const Istream &) = delete;

    virtual ~Istream();

    struct pool &GetPool() {
        return pool;
    }

    FdTypeMask GetHandlerDirect() const {
        return handler_direct;
    }

    bool CheckDirect(FdType type) const {
        return (handler_direct & FdTypeMask(type)) != 0;
    }

    size_t InvokeData(const void *data, size_t length);
    ssize_t InvokeDirect(FdType type, int fd, size_t max_length);
    void InvokeEof();
    void InvokeError(GError *error);

    void Destroy() {
        this->~Istream();
        /* no need to free memory from the pool */
    }

    void DestroyEof() {
        InvokeEof();
        Destroy();
    }

    void DestroyError(GError *error) {
        InvokeError(error);
        Destroy();
    }

    /**
     * @return the number of bytes still in the buffer
     */
    template<typename Buffer>
    size_t ConsumeFromBuffer(Buffer &buffer) {
        auto r = buffer.Read().ToVoid();
        if (r.IsEmpty())
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
    size_t SendFromBuffer(Buffer &buffer) {
        auto r = buffer.Read().ToVoid();
        if (r.IsEmpty())
            return 0;

        size_t consumed = InvokeData(r.data, r.size);
        if (consumed > 0)
            buffer.Consume(consumed);
        return consumed;
    }

public:
    struct istream *Cast() {
        return this;
    }

    static constexpr Istream &Cast(struct istream &i) {
        return (Istream &)i;
    }

    static constexpr Istream *Cast(struct istream *i) {
        return (Istream *)i;
    }

public:
    bool HasHandler() const {
        return handler != nullptr;
    }

    void SetHandler(const struct istream_handler &_handler,
                    void *_handler_ctx,
                    FdTypeMask _handler_direct=0) {
        assert(!destroyed);
        assert(_handler.data != nullptr);
        assert(_handler.eof != nullptr);
        assert(_handler.abort != nullptr);

        handler = &_handler;
        handler_ctx = _handler_ctx;
        handler_direct = _handler_direct;
    }

    void ClearHandler() {
        assert(!destroyed);
        assert(!eof);
        assert(HasHandler());

        handler = nullptr;
    }

    void SetDirect(FdTypeMask _handler_direct) {
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
    off_t GetAvailable(bool partial) {
#ifndef NDEBUG
        assert(!destroyed);
        assert(!closing);
        assert(!eof);
        assert(!reading);

        struct pool_notify_state notify;
        pool_notify(&pool, &notify);
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
    off_t Skip(off_t length) {
#ifndef NDEBUG
        assert(!destroyed);
        assert(!closing);
        assert(!eof);
        assert(!reading);

        struct pool_notify_state notify;
        pool_notify(&pool, &notify);
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
    void Read() {
#ifndef NDEBUG
        assert(!destroyed);
        assert(!closing);
        assert(!eof);
        assert(!reading);
        assert(!in_data);

        struct pool_notify_state notify;
        pool_notify(&pool, &notify);
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
     * Close the istream object, and return the remaining data as a
     * file descriptor.  This fd can be read until end-of-stream.
     * Returns -1 if this is not possible (the stream object is still
     * usable).
     */
    int AsFd() {
#ifndef NDEBUG
        assert(!destroyed);
        assert(!closing);
        assert(!eof);
        assert(!reading);
        assert(!in_data);

        struct pool_notify_state notify;
        pool_notify(&pool, &notify);
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
    void Close() {
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
    void CloseUnused() {
        assert(!HasHandler());

        Close();
    }

protected:
    virtual off_t _GetAvailable(gcc_unused bool partial) {
        return -1;
    }

    virtual off_t _Skip(gcc_unused off_t length) {
        return -1;
    }

    virtual void _Read() = 0;

    virtual int _AsFd() {
        return -1;
    }

    virtual void _Close() {
        Destroy();
    }
};

#include "Invoke.hxx"

template<typename T, typename... Args>
static inline struct istream *
NewIstream(struct pool &pool, Args&&... args)
{
    return NewFromPool<T>(pool, pool,
                          std::forward<Args>(args)...)->Cast();
}

#endif
