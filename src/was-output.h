/*
 * Web Application Socket protocol, output data channel library.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_OUTPUT_H
#define BENG_PROXY_WAS_OUTPUT_H

#include "istream.h"

#include <glib.h>
#include <stdint.h>

struct was_output_handler {
    /**
     * Announces the length of the resource.
     *
     * @param true on success, false if the was_output object has been
     * deleted
     */
    bool (*length)(uint64_t length, void *ctx);

    /**
     * The stream ended prematurely, but the was_output object is
     * still ok.
     *
     * @param the number of bytes aready sent
     * @param true on success, false if the was_output object has been
     * deleted
     */
    bool (*premature)(uint64_t length, GError *error, void *ctx);

    void (*eof)(void *ctx);
    void (*abort)(GError *error, void *ctx);
};

struct was_output *
was_output_new(pool_t pool, int fd, istream_t input,
               const struct was_output_handler *handler, void *handler_ctx);

uint64_t
was_output_free(struct was_output *data);

static inline uint64_t
was_output_free_p(struct was_output **output_p)
{
    struct was_output *output = *output_p;
    *output_p = NULL;
    return was_output_free(output);
}

#endif
