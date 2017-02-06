/*
 * Glue code for using the ssl_filter in a client connection.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_CLIENT_HXX
#define BENG_PROXY_SSL_CLIENT_HXX

struct pool;
struct SocketFilter;
class EventLoop;

void
ssl_client_init();

void
ssl_client_deinit();

const SocketFilter &
ssl_client_get_filter();

/**
 *
 * Throws std::runtime_error on error.
 */
void *
ssl_client_create(struct pool *pool, EventLoop &event_loop,
                  const char *hostname);

#endif
