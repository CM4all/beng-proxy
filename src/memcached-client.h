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

/**
 * Invoke a call to the memcached server.  The result will be
 * delivered to the specified callback function.
 *
 * @param pool the memory pool used by this function
 * @param fd a socket to the memcached server
 * @param lease the lease for the socket
 * @param lease_ctx a context pointer for the lease
 * @param opcode the opcode of the memcached method
 * @param extras optional extra data for the request
 * @param extras_length the length of the extra data
 * @param key key for the request
 * @param key_length the length of the key
 * @param value an optional request value
 * @param handler a callback function which receives the response
 * @param handler_ctx a context pointer for the callback function
 * @param async_ref a handle which may be used to abort the operation
 */
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
