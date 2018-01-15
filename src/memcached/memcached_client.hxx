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
 * memcached client implementation.
 */

#ifndef MEMCACHED_CLIENT_HXX
#define MEMCACHED_CLIENT_HXX

#include "io/FdType.hxx"
#include "memcached_protocol.hxx"

#include <exception>

#include <stddef.h>

static constexpr size_t MEMCACHED_EXTRAS_MAX = 0xff;
static constexpr size_t MEMCACHED_KEY_MAX = 0x7fff;

struct pool;
class EventLoop;
class UnusedIstreamPtr;
class SocketDescriptor;
class Lease;
class HttpResponseHandler;
class StringMap;
class CancellablePointer;

struct memcached_client_handler {
    void (*response)(enum memcached_response_status status,
                     const void *extras, size_t extras_length,
                     const void *key, size_t key_length,
                     UnusedIstreamPtr value, void *ctx);

    void (*error)(std::exception_ptr ep, void *ctx);
};

/**
 * Invoke a call to the memcached server.  The result will be
 * delivered to the specified callback function.
 *
 * @param pool the memory pool used by this function
 * @param fd a socket to the memcached server
 * @param lease the lease for the socket
 * @param opcode the opcode of the memcached method
 * @param extras optional extra data for the request
 * @param extras_length the length of the extra data
 * @param key key for the request
 * @param key_length the length of the key
 * @param value an optional request value
 * @param handler a callback function which receives the response
 * @param handler_ctx a context pointer for the callback function
 * @param cancel_ptr a handle which may be used to abort the operation
 */
void
memcached_client_invoke(struct pool *pool, EventLoop &event_loop,
                        SocketDescriptor fd, FdType fd_type,
                        Lease &lease,
                        enum memcached_opcode opcode,
                        const void *extras, size_t extras_length,
                        const void *key, size_t key_length,
                        UnusedIstreamPtr value,
                        const struct memcached_client_handler *handler,
                        void *handler_ctx,
                        CancellablePointer &cancel_ptr);

#endif
