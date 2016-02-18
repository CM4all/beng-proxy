/*
 * Stock of connections to a memcached server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef MEMCACHED_STOCK_HXX
#define MEMCACHED_STOCK_HXX

#include "memcached_protocol.hxx"

#include <stddef.h>

struct pool;
class Istream;
struct memcached_client_handler;
struct memcached_stock;
struct TcpBalancer;
struct AddressList;
struct async_operation_ref;

struct memcached_stock *
memcached_stock_new(TcpBalancer *tcp_balancer,
                    const AddressList *address);

void
memcached_stock_free(struct memcached_stock *stock);

/**
 * Invoke a call to the memcached server, on a socket to be obtained
 * from the #memcached_stock.  See memcached_client_invoke() for a
 * description of the other arguments.
 */
void
memcached_stock_invoke(struct pool *pool, struct memcached_stock *stock,
                       enum memcached_opcode opcode,
                       const void *extras, size_t extras_length,
                       const void *key, size_t key_length,
                       Istream *value,
                       const struct memcached_client_handler *handler,
                       void *handler_ctx,
                       struct async_operation_ref *async_ref);

#endif
