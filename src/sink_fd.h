/*
 * An istream handler which sends data to a file descriptor.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SINK_FD_H
#define BENG_PROXY_SINK_FD_H

#include "istream-direct.h"

#include <glib.h>
#include <stdbool.h>

struct pool;
struct istream;

struct sink_fd_handler {
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

#ifdef __cplusplus
extern "C" {
#endif

struct sink_fd *
sink_fd_new(struct pool *pool, struct istream *istream,
            int fd, enum istream_direct fd_type,
            const struct sink_fd_handler *handler, void *ctx);

void
sink_fd_read(struct sink_fd *ss);

void
sink_fd_close(struct sink_fd *ss);

#ifdef __cplusplus
}
#endif

#endif
