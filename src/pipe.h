/*
 * Filter an istream through a piped program.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PIPE_H
#define BENG_PIPE_H

#include "istream.h"

struct strmap;
struct http_response_handler;

void
pipe_filter(pool_t pool, const char *path,
            const char *const* args, unsigned num_args,
            struct strmap *headers, istream_t body,
            const struct http_response_handler *handler,
            void *handler_ctx);

#endif
