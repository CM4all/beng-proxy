/*
 * An istream handler which sends data to a file descriptor.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SINK_FD_HXX
#define BENG_PROXY_SINK_FD_HXX

#include "FdType.hxx"
#include "glibfwd.hxx"

struct pool;
class EventLoop;
class Istream;
struct SinkFd;

struct SinkFdHandler {
    /**
     * Called when end-of-file has been received from the istream.
     */
    void (*input_eof)(void *ctx);

    /**
     * Called when an error has been reported by the istream, right
     * before the sink is destructed.
     */
    void (*input_error)(GError *error, void *ctx);

    /**
     * Called when a send error has occurred on the socket, right
     * before the sink is destructed.
     *
     * @return true to close the stream, false when this method has
     * already destructed the sink
     */
    bool (*send_error)(int error, void *ctx);
};

SinkFd *
sink_fd_new(EventLoop &event_loop, struct pool &pool, Istream &istream,
            int fd, FdType fd_type,
            const SinkFdHandler &handler, void *ctx);

void
sink_fd_read(SinkFd *ss);

void
sink_fd_close(SinkFd *ss);

#endif
