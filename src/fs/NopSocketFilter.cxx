/*
 * Copyright 2007-2018 Content Management AG
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

#include "NopSocketFilter.hxx"
#include "FilteredSocket.hxx"
#include "pool/pool.hxx"

struct nop_socket_filter {
    FilteredSocket *socket;
};

/*
 * SocketFilter
 *
 */

static void
nop_socket_filter_init(FilteredSocket &s, void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    f->socket = &s;
}

static BufferedResult
nop_socket_filter_data(const void *data, size_t length, void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return f->socket->InvokeData(data, length);
}

static bool
nop_socket_filter_is_empty(void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return f->socket->InternalIsEmpty();
}

static bool
nop_socket_filter_is_full(void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return f->socket->InternalIsFull();
}

static size_t
nop_socket_filter_available(void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return f->socket->InternalGetAvailable();
}

static void
nop_socket_filter_consumed(size_t nbytes, void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    f->socket->InternalConsumed(nbytes);
}

static bool
nop_socket_filter_read(bool expect_more, void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return f->socket->InternalRead(expect_more);
}

static ssize_t
nop_socket_filter_write(const void *data, size_t length, void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return f->socket->InternalWrite(data, length);
}

static bool
nop_socket_filter_internal_write(void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return f->socket->InvokeWrite();
}

static void
nop_socket_filter_closed(void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;
    (void)f;
}

static bool
nop_socket_filter_remaining(size_t remaining, void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    return f->socket->InvokeRemaining(remaining);
}

static void
nop_socket_filter_end(void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    f->socket->InvokeEnd();
}

static void
nop_socket_filter_close(void *ctx)
{
    struct nop_socket_filter *f = (struct nop_socket_filter *)ctx;

    (void)f;
}

const SocketFilter nop_socket_filter = {
    .init = nop_socket_filter_init,
    .set_handshake_callback = nullptr,
    .data = nop_socket_filter_data,
    .is_empty = nop_socket_filter_is_empty,
    .is_full = nop_socket_filter_is_full,
    .available = nop_socket_filter_available,
    .consumed = nop_socket_filter_consumed,
    .read = nop_socket_filter_read,
    .write = nop_socket_filter_write,
    .schedule_read = nullptr,
    .schedule_write = nullptr,
    .unschedule_write = nullptr,
    .internal_write = nop_socket_filter_internal_write,
    .closed = nop_socket_filter_closed,
    .remaining = nop_socket_filter_remaining,
    .end = nop_socket_filter_end,
    .close = nop_socket_filter_close,
};

/*
 * constructor
 *
 */

void *
nop_socket_filter_new(struct pool &pool)
{
    return NewFromPool<struct nop_socket_filter>(pool);
}
