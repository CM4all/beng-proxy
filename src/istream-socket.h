/*
 * An istream receiving data from a socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_GB_H
#define BENG_PROXY_ISTREAM_GB_H

#include "istream.h"

struct growing_buffer;

struct istream_socket_handler {
    /**
     * Called when the buffer is full, but the handler method did not
     * consume any of it.  This is never called for "direct" transfer,
     * because there is no buffer in that mode.
     *
     * This method is optional.
     *
     * @return false when the istream has been closed
     */
    bool (*full)(void *ctx);

    /**
     * Called when data is being requested, but the socket does not
     * deliver.  This may cause some action in the caller that may
     * bring more data into the other side of the socket.
     *
     * This method is optional.
     */
    void (*read)(void *ctx);

    /**
     * The istream handler has requested closing the socket.
     */
    void (*close)(void *ctx);

    /**
     * Called when a receive error has occurred on the socket.  The
     * socket will not be used anymore, and the stream is closed.
     *
     * @return true to propagate the error to the istream handler,
     * false when the istream has been closed
     */
    bool (*error)(int error, void *ctx);

    /**
     * Called when the end of the stream has been reached.  The socket
     * will not be used anymore, but there may still be data in the
     * buffer.  The method finished() will be called once the buffer
     * is empty.
     *
     * @return false when the istream has been closed
     */
    bool (*depleted)(void *ctx);

    /**
     * Called after depleted(), as soon as the buffer is drained.
     *
     * @return false when the istream has been closed
     */
    bool (*finished)(void *ctx);
};

struct istream *
istream_socket_new(struct pool *pool, int fd, istream_direct_t fd_type,
                   const struct istream_socket_handler *handler, void *ctx);

#endif
