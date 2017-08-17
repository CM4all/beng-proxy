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

#include "sink_fd.hxx"
#include "Sink.hxx"
#include "pool.hxx"
#include "direct.hxx"
#include "io/Splice.hxx"
#include "io/FileDescriptor.hxx"
#include "event/SocketEvent.hxx"

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>

struct SinkFd final : IstreamSink {
    struct pool *pool;

    FileDescriptor fd;
    FdType fd_type;
    const SinkFdHandler *handler;
    void *handler_ctx;

    SocketEvent event;

    /**
     * Set to true each time data was received from the istream.
     */
    bool got_data;

    /**
     * This flag is used to determine if the SocketEvent::WRITE event
     * shall be scheduled after a splice().  We need to add the event
     * only if the splice() was triggered by SocketEvent::WRITE,
     * because then we're responsible for querying more data.
     */
    bool got_event = false;

#ifndef NDEBUG
    bool valid = true;
#endif

    SinkFd(EventLoop &event_loop, struct pool &_pool, Istream &_istream,
           FileDescriptor _fd, FdType _fd_type,
           const SinkFdHandler &_handler, void *_handler_ctx)
        :IstreamSink(_istream, istream_direct_mask_to(_fd_type)),
         pool(&_pool),
         fd(_fd), fd_type(_fd_type),
         handler(&_handler), handler_ctx(_handler_ctx),
         event(event_loop, fd.Get(), SocketEvent::WRITE|SocketEvent::PERSIST,
               BIND_THIS_METHOD(EventCallback)) {
        ScheduleWrite();
    }

    bool IsDefined() const {
        return input.IsDefined();
    }

    void Read() {
        input.Read();
    }

    void Close() {
        input.Close();
    }

    void ScheduleWrite() {
        assert(fd.IsDefined());
        assert(input.IsDefined());

        got_event = false;
        event.Add();
    }

    void EventCallback(unsigned events);

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) override;
    ssize_t OnDirect(FdType type, int fd, size_t max_length) override;
    void OnEof() override;
    void OnError(std::exception_ptr ep) override;
};

/*
 * istream handler
 *
 */

inline size_t
SinkFd::OnData(const void *data, size_t length)
{
    got_data = true;

    ssize_t nbytes = IsAnySocket(fd_type)
        ? send(fd.Get(), data, length, MSG_DONTWAIT|MSG_NOSIGNAL)
        : fd.Write(data, length);
    if (nbytes >= 0) {
        ScheduleWrite();
        return nbytes;
    } else if (errno == EAGAIN) {
        ScheduleWrite();
        return 0;
    } else {
        event.Delete();
        if (handler->send_error(errno, handler_ctx))
            input.Close();
        return 0;
    }
}

inline ssize_t
SinkFd::OnDirect(FdType type, int _fd, size_t max_length)
{
    got_data = true;

    ssize_t nbytes = SpliceTo(_fd, type, fd.Get(), fd_type, max_length);
    if (unlikely(nbytes < 0 && errno == EAGAIN)) {
        if (!fd.IsReadyForWriting()) {
            ScheduleWrite();
            return ISTREAM_RESULT_BLOCKING;
        }

        /* try again, just in case connection->fd has become ready
           between the first istream_direct_to_socket() call and
           fd_ready_for_writing() */
        nbytes = SpliceTo(_fd, type, fd.Get(), fd_type, max_length);
    }

    if (likely(nbytes > 0) && (got_event || type == FdType::FD_FILE))
        /* regular files don't have support for SocketEvent::READ, and
           thus the sink is responsible for triggering the next
           splice */
        ScheduleWrite();

    return nbytes;
}

inline void
SinkFd::OnEof()
{
    got_data = true;

#ifndef NDEBUG
    valid = false;
#endif

    event.Delete();

    handler->input_eof(handler_ctx);
}

inline void
SinkFd::OnError(std::exception_ptr ep)
{
    got_data = true;

#ifndef NDEBUG
    valid = false;
#endif

    event.Delete();

    handler->input_error(ep, handler_ctx);
}

/*
 * libevent callback
 *
 */

inline void
SinkFd::EventCallback(unsigned)
{
    pool_ref(pool);

    got_event = true;
    got_data = false;
    input.Read();

    if (!got_data)
        /* the fd is ready for writing, but the istream is blocking -
           don't try again for now */
        event.Delete();

    pool_unref(pool);
}

/*
 * constructor
 *
 */

SinkFd *
sink_fd_new(EventLoop &event_loop, struct pool &pool, Istream &istream,
            FileDescriptor fd, FdType fd_type,
            const SinkFdHandler &handler, void *ctx)
{
    assert(fd.IsDefined());
    assert(handler.input_eof != nullptr);
    assert(handler.input_error != nullptr);
    assert(handler.send_error != nullptr);

    return NewFromPool<SinkFd>(pool, event_loop, pool, istream, fd, fd_type,
                               handler, ctx);
}

void
sink_fd_read(SinkFd *ss)
{
    assert(ss != nullptr);
    assert(ss->valid);
    assert(ss->IsDefined());

    ss->Read();
}

void
sink_fd_close(SinkFd *ss)
{
    assert(ss != nullptr);
    assert(ss->valid);
    assert(ss->IsDefined());

#ifndef NDEBUG
    ss->valid = false;
#endif

    ss->event.Delete();
    ss->Close();
}
