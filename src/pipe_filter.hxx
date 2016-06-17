/*
 * Filter an istream through a piped program.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PIPE_FILTER_HXX
#define BENG_PIPE_FILTER_HXX

#include <http/status.h>

struct pool;
class Istream;
class EventLoop;
class SpawnService;
struct strmap;
struct http_response_handler;
struct ChildOptions;
template<typename T> struct ConstBuffer;

/**
 * @param status the HTTP status code to be sent to the response
 * handler
 */
void
pipe_filter(SpawnService &spawn_service, EventLoop &event_loop,
            struct pool *pool, const char *path,
            ConstBuffer<const char *> args,
            const ChildOptions &options,
            http_status_t status, struct strmap *headers, Istream *body,
            const struct http_response_handler *handler,
            void *handler_ctx);

#endif
