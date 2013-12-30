/*
 * Filter an istream through a piped program.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PIPE_H
#define BENG_PIPE_H

#include <http/status.h>

struct pool;
struct istream;
struct strmap;
struct http_response_handler;
struct namespace_options;

/**
 * @param status the HTTP status code to be sent to the response
 * handler
 */
void
pipe_filter(struct pool *pool, const char *path,
            const char *const* args, unsigned num_args,
            const struct namespace_options *ns,
            http_status_t status, struct strmap *headers, struct istream *body,
            const struct http_response_handler *handler,
            void *handler_ctx);

#endif
