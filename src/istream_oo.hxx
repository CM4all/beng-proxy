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

protected:
    Istream(struct pool &pool, const struct istream_class &cls)
        :output(pool, cls) {}

    istream_direct_t GetHandlerDirect() const {
        return output.handler_direct;
    }

    size_t InvokeData(const void *data, size_t length) {
        return istream_invoke_data(&output, data, length);
    }

    ssize_t InvokeDirect(enum istream_direct type, int fd, size_t max_length) {
        return istream_invoke_direct(&output, type, fd, max_length);
    }

    void Deinit() {
        istream_deinit(&output);
    }

    void DeinitEof() {
        istream_deinit_eof(&output);
    }

    void DeinitError(GError *error) {
        istream_deinit_abort(&output, error);
    }

public:
    struct istream *Cast() {
        return &output;
    }

    static constexpr Istream &Cast(struct istream &i) {
        return ContainerCast2(i, &Istream::output);
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
class MakeIstreamClass {
    static constexpr T &Cast(struct istream &i) {
        //return ContainerCast2(i, &T::output);
        return (T &)Istream::Cast(i);
    }

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

public:
    static const struct istream_class cls;
};

template<typename T>
constexpr struct istream_class MakeIstreamClass<T>::cls = {
    GetAvailable,
    Skip,
    Read,
    AsFd,
    Close,
};

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
