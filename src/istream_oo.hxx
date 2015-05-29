/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ISTREAM_OO_HXX
#define ISTREAM_OO_HXX

#include "pool.hxx"
#include "istream.hxx"
#include "istream_invoke.hxx"
#include "istream_new.hxx"
#include "util/Cast.hxx"

class Istream {
    struct istream output;

    static const struct istream_class cls;

protected:
    explicit Istream(struct pool &pool)
        :output(pool, cls) {}

    Istream(const Istream &) = delete;
    Istream &operator=(const Istream &) = delete;

    virtual ~Istream() {
        istream_deinit(&output);
    }

    struct pool &GetPool() {
        return *output.pool;
    }

    bool HasHandler() const {
        return istream_has_handler(&output);
    }

    istream_direct_t GetHandlerDirect() const {
        return output.handler_direct;
    }

    size_t InvokeData(const void *data, size_t length) {
        return istream_invoke_data(&output, data, length);
    }

    ssize_t InvokeDirect(enum istream_direct type, int fd, size_t max_length) {
        return istream_invoke_direct(&output, type, fd, max_length);
    }

    void InvokeEof() {
        istream_invoke_eof(&output);
    }

    void InvokeError(GError *error) {
        istream_invoke_abort(&output, error);
    }

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

public:
    struct istream *Cast() {
        return &output;
    }

    static constexpr Istream &Cast(struct istream &i) {
        return ContainerCast2(i, &Istream::output);
    }

    /* istream */

    virtual off_t GetAvailable(gcc_unused bool partial) {
        return -1;
    }

    virtual off_t Skip(gcc_unused off_t length) {
        return -1;
    }

    virtual void Read() = 0;

    virtual int AsFd() {
        return -1;
    }

    virtual void Close() {
        Destroy();
    }

private:
    static off_t GetAvailable(struct istream *istream, bool partial) {
        return Cast(*istream).GetAvailable(partial);
    }

    static off_t Skip(struct istream *istream, off_t length) {
        return Cast(*istream).Skip(length);
    }

    static void Read(struct istream *istream) {
        Cast(*istream).Read();
    }

    static int AsFd(struct istream *istream) {
        return Cast(*istream).AsFd();
    }

    static void Close(struct istream *istream) {
        Cast(*istream).Close();
    }
};

template<typename T, typename... Args>
static inline struct istream *
NewIstream(struct pool &pool, Args&&... args)
{
    return NewFromPool<T>(pool, pool,
                          std::forward<Args>(args)...)->Cast();
}

template<typename T>
class MakeIstreamHandler {
    static constexpr T &Cast(void *ctx) {
        return *(T *)ctx;
    }

    static size_t Data(const void *data, size_t length, void *ctx) {
        return Cast(ctx).OnData(data, length);
    }

    static ssize_t Direct(enum istream_direct type, int fd, size_t max_length,
                          void *ctx) {
        return Cast(ctx).OnDirect(type, fd, max_length);
    }

    static void Eof(void *ctx) {
        return Cast(ctx).OnEof();
    }

    static void Error(GError *error, void *ctx) {
        return Cast(ctx).OnError(error);
    }

public:
    static const struct istream_handler handler;
};

template<typename T>
constexpr struct istream_handler MakeIstreamHandler<T>::handler = {
    Data,
    Direct,
    Eof,
    Error,
};

#endif
