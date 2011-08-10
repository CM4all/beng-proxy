/*
 * An istream handler which sends data to a socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SINK_SOCKET_H
#define BENG_PROXY_SINK_SOCKET_H

#include "istream.h"

struct pool;

struct sink_socket_handler {
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

struct sink_socket *
sink_socket_new(struct pool *pool, struct istream *istream,
                int fd, enum istream_direct fd_type,
                const struct sink_socket_handler *handler, void *ctx);

void
sink_socket_read(struct sink_socket *ss);

void
sink_socket_close(struct sink_socket *ss);

#endif
