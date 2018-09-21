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

#ifndef BENG_PROXY_HTTP_BODY_HXX
#define BENG_PROXY_HTTP_BODY_HXX

#include "istream/istream_dechunk.hxx"
#include "istream/istream.hxx"
#include "istream/Bucket.hxx"

#include "util/Compiler.h"

#include <assert.h>
#include <stddef.h>

struct pool;
class EventLoop;
class SocketDescriptor;

/**
 * Utilities for reading a HTTP body, either request or response.
 */
class HttpBodyReader : public Istream, DechunkHandler {
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

    /**
     * The remaining number of bytes.
     *
     * @see #REST_UNKNOWN, #REST_EOF_CHUNK,
     * #REST_CHUNKED
     */
    off_t rest;

    bool end_seen;

public:
    explicit HttpBodyReader(struct pool &_pool)
        :Istream(_pool) {}

    UnusedIstreamPtr Init(EventLoop &event_loop, off_t content_length,
                          bool chunked);

    using Istream::GetPool;
    using Istream::Destroy;

    void InvokeEof() {
        assert(IsEOF());

        /* suppress InvokeEof() if rest==REST_EOF_CHUNK because in
           that case, the dechunker has already emitted that event */
        if (rest == 0)
            Istream::InvokeEof();
    }

    void DestroyEof() {
        InvokeEof();
        Destroy();
    }

    using Istream::InvokeError;
    using Istream::DestroyError;

    bool IsChunked() const {
        return rest == REST_CHUNKED || rest == REST_EOF_CHUNK;
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

    bool GotEndChunk() const {
        return rest == REST_EOF_CHUNK;
    }

    /**
     * Do we require more data to finish the body?
     */
    bool RequireMore() const {
        return rest > 0 || (rest == REST_CHUNKED && !end_seen);
    }

    template<typename Socket>
    gcc_pure
    off_t GetAvailable(const Socket &s, bool partial) const {
        assert(rest != REST_EOF_CHUNK);

        if (KnownLength())
            return rest;

        return partial
            ? (off_t)s.GetAvailable()
            : -1;
    }

    template<typename Socket>
    void FillBucketList(const Socket &s, IstreamBucketList &list) {
        auto b = s.ReadBuffer();
        if (b.empty()) {
            if (!IsEOF())
                list.SetMore();
            return;
        }

        size_t max = GetMaxRead(b.size);
        if (b.size > max)
            b.size = max;

        list.Push(ConstBuffer<void>(b.data, b.size));
        if ((off_t)b.size != rest)
            list.SetMore();
    }

    template<typename Socket>
    size_t ConsumeBucketList(Socket &s, size_t nbytes) {
        auto b = s.ReadBuffer();
        if (b.empty())
            return 0;

        size_t max = GetMaxRead(b.size);
        if (nbytes > max)
            nbytes = max;
        if (nbytes == 0)
            return 0;

        s.DisposeConsumed(nbytes);
        Consumed(nbytes);
        Istream::Consumed(nbytes);
        return nbytes;
    }

    size_t FeedBody(const void *data, size_t length);

    using Istream::CheckDirect;

    ssize_t TryDirect(SocketDescriptor fd, FdType fd_type);

    /**
     * Determines whether the socket can be released now.  This is true if
     * the body is empty, or if the data in the buffer contains enough for
     * the full response.
     */
    template<typename Socket>
    gcc_pure
    bool IsSocketDone(const Socket &s) const {
        if (IsChunked())
            return end_seen;

        return KnownLength() && (off_t)s.GetAvailable() >= rest;
    }

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

protected:
    /* virtual methods from class DechunkHandler */
    void OnDechunkEndSeen() final;
    bool OnDechunkEnd() final;
};

#endif
