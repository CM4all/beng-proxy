/*
 * Filter an istream through a piped program.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PIPE_FILTER_HXX
#define BENG_PIPE_FILTER_HXX

#include <http/status.h>

struct pool;
struct istream;
struct strmap;
struct http_response_handler;
struct child_options;
template<typename T> struct ConstBuffer;

/**
 * @param status the HTTP status code to be sent to the response
 * handler
 */
void
pipe_filter(struct pool *pool, const char *path,
            ConstBuffer<const char *> args,
            ConstBuffer<const char *> env,
            const struct child_options &options,
            http_status_t status, struct strmap *headers, struct istream *body,
            const struct http_response_handler *handler,
            void *handler_ctx);

#endif
