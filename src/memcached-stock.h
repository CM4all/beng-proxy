/*
 * Stock of connections to a memcached server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef MEMCACHED_STOCK_H
#define MEMCACHED_STOCK_H

#include "memcached-client.h"

struct memcached_stock;
struct hstock;
struct uri_with_address;

struct memcached_stock *
memcached_stock_new(pool_t pool, struct hstock *tcp_stock,
                    struct uri_with_address *address);

void
memcached_stock_free(struct memcached_stock *stock);

void
memcached_stock_invoke(pool_t pool, struct memcached_stock *stock,
                       enum memcached_opcode opcode,
                       const void *extras, size_t extras_length,
                       const void *key, size_t key_length,
                       const void *value, size_t value_length,
                       memcached_response_handler_t handler,
                       void *handler_ctx,
                       struct async_operation_ref *async_ref);

#endif
