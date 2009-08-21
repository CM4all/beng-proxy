/*
 * memcached client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef MEMCACHED_CLIENT_H
#define MEMCACHED_CLIENT_H

#include "istream.h"
#include "memcached-protocol.h"

struct lease;
struct http_response_handler;
struct strmap;

typedef void (*memcached_response_handler_t)(enum memcached_response_status status,
                                             const void *extras, size_t extras_length,
                                             const void *key, size_t key_length,
                                             istream_t value, void *ctx);

void
memcached_client_invoke(pool_t pool, int fd,
                        const struct lease *lease, void *lease_ctx,
                        enum memcached_opcode opcode,
                        const void *extras, size_t extras_length,
                        const void *key, size_t key_length,
                        istream_t value,
                        memcached_response_handler_t handler, void *handler_ctx,
                        struct async_operation_ref *async_ref);

#endif
