/*
 * memcached client implementation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef MEMCACHED_CLIENT_HXX
#define MEMCACHED_CLIENT_HXX

#include "istream-direct.h"
#include "memcached_protocol.hxx"

#include <glib.h>
#include <stddef.h>

static constexpr size_t MEMCACHED_EXTRAS_MAX = 0xff;
static constexpr size_t MEMCACHED_KEY_MAX = 0x7fff;

struct pool;
struct istream;
struct lease;
struct http_response_handler;
struct strmap;
struct async_operation_ref;

struct memcached_client_handler {
    void (*response)(enum memcached_response_status status,
                     const void *extras, size_t extras_length,
                     const void *key, size_t key_length,
                     struct istream *value, void *ctx);

    void (*error)(GError *error, void *ctx);
};

G_GNUC_CONST
static inline GQuark
memcached_client_quark(void)
{
    return g_quark_from_static_string("memcached_client");
}

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
memcached_client_invoke(struct pool *pool, int fd, enum istream_direct fd_type,
                        const struct lease *lease, void *lease_ctx,
                        enum memcached_opcode opcode,
                        const void *extras, size_t extras_length,
                        const void *key, size_t key_length,
                        struct istream *value,
                        const struct memcached_client_handler *handler,
                        void *handler_ctx,
                        struct async_operation_ref *async_ref);

#endif
