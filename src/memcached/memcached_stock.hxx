/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Stock of connections to a memcached server.
 */

#ifndef MEMCACHED_STOCK_HXX
#define MEMCACHED_STOCK_HXX

#include "memcached_protocol.hxx"

#include <stddef.h>

struct pool;
class EventLoop;
class UnusedIstreamPtr;
struct memcached_client_handler;
struct MemachedStock;
class TcpBalancer;
struct AddressList;
class CancellablePointer;

MemachedStock *
memcached_stock_new(EventLoop &event_loop, TcpBalancer &tcp_balancer,
                    const AddressList &address);

void
memcached_stock_free(MemachedStock *stock);

/**
 * Invoke a call to the memcached server, on a socket to be obtained
 * from the #memcached_stock.  See memcached_client_invoke() for a
 * description of the other arguments.
 */
void
memcached_stock_invoke(struct pool &pool, MemachedStock &stock,
                       enum memcached_opcode opcode,
                       const void *extras, size_t extras_length,
                       const void *key, size_t key_length,
                       UnusedIstreamPtr value,
                       const struct memcached_client_handler &handler,
                       void *handler_ctx,
                       CancellablePointer &cancel_ptr);

#endif
