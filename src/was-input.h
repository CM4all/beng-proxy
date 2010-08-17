/*
 * Web Application Socket protocol, input data channel library.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_INPUT_H
#define BENG_PROXY_WAS_INPUT_H

#include "istream.h"

#include <stdint.h>

struct was_input_handler {
    /**
     * Announce the input length.
     */
    void (*input_length)(uint64_t length, void *ctx);

    void (*eof)(void *ctx);
    void (*abort)(void *ctx);
};

struct was_input *
was_input_new(pool_t pool, int fd,
              const struct was_input_handler *handler, void *handler_ctx);

void
was_input_free(struct was_input *input);

static inline void
was_input_free_p(struct was_input **input_p)
{
    struct was_input *input = *input_p;
    *input_p = NULL;
    was_input_free(input);
}

istream_t
was_input_enable(struct was_input *input);

/**
 * Set the new content length of this entity.
 *
 * @return false if the value is invalid (callback "abort" has been
 * invoked in this case)
 */
bool
was_input_set_length(struct was_input *input, uint64_t length);

void
was_input_enable_timeout(struct was_input *input);

bool
was_input_discard_output(struct was_input *input);

#endif
