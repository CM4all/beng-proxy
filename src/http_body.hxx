/*
 * Utilities for reading a HTTP body, either request or response.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_BODY_HXX
#define BENG_PROXY_HTTP_BODY_HXX

#include "istream.h"
#include "util/Cast.hxx"

#include <inline/compiler.h>

#include <stddef.h>

struct pool;
struct FilteredSocket;

class HttpBodyReader {
    /**
     * The remaining size is unknown.
     */
    static constexpr off_t REST_UNKNOWN = -1;

    /**
     * EOF chunk has been seen.
     */
    static constexpr off_t REST_EOF_CHUNK = -2;

    /**
     * Chunked response.  Will flip to #REST_EOF_CHUNK as soon
     * as the EOF chunk is seen.
     */
    static constexpr off_t REST_CHUNKED = -3;

    struct istream output;

    /**
     * The remaining number of bytes.
     *
     * @see #REST_UNKNOWN, #REST_EOF_CHUNK,
     * #REST_CHUNKED
     */
    off_t rest;

#ifndef NDEBUG
    bool chunked, socket_eof;
#endif

public:
    struct istream &Init(const struct istream_class &stream,
                         struct pool &stream_pool,
                         struct pool &pool, off_t content_length,
                         bool chunked);

    struct istream &GetStream() {
        return output;
    }

    static HttpBodyReader &FromStream(struct istream &stream) {
        return ContainerCast2(stream, &HttpBodyReader::output);
    }

    void Deinit();
    void DeinitEOF();
    void DeinitAbort(GError *error);

    bool IsChunked() const {
        return rest == REST_CHUNKED;
    }

    /**
     * Do we know the remaining length of the body?
     */
    bool KnownLength() const {
        return rest >= 0;
    }

    bool IsEOF() const {
        return rest == 0 || rest == REST_EOF_CHUNK;
    }

    /**
     * Do we require more data to finish the body?
     */
    bool RequireMore() const {
        return rest > 0 || rest == REST_CHUNKED;
    }

    gcc_pure
    off_t GetAvailable(const FilteredSocket &s, bool partial) const;

    size_t FeedBody(const void *data, size_t length);

    gcc_pure
    bool CheckDirect(enum istream_direct fd_type) const;

    ssize_t TryDirect(int fd, enum istream_direct fd_type);

    /**
     * Determines whether the socket can be released now.  This is true if
     * the body is empty, or if the data in the buffer contains enough for
     * the full response.
     */
    gcc_pure
    bool IsSocketDone(const FilteredSocket &s) const;

    /**
     * The underlying socket has been closed by the remote.
     *
     * @return true if there is data left in the buffer, false if the body
     * has been finished (with or without error)
     */
    bool SocketEOF(size_t remaining);

private:
    gcc_pure
    size_t GetMaxRead(size_t length) const;

    void Consumed(size_t nbytes);

    void DechunkerEOF();
    static void DechunkerEOF(void *ctx);
};

#endif
