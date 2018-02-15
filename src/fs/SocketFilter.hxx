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

#pragma once

#include "util/BindMethod.hxx"

#include <sys/types.h>
#include <stddef.h>

enum class BufferedResult;
struct FilteredSocket;

struct SocketFilter {
    void (*init)(FilteredSocket &s, void *ctx);

    /**
     * @see FilteredSocket::SetHandshakeCallback()
     */
    void (*set_handshake_callback)(BoundMethod<void()> callback, void *ctx);

    /**
     * Data has been read from the socket into the input buffer.  Call
     * filtered_socket_internal_consumed() each time you consume data
     * from the given buffer.
     */
    BufferedResult (*data)(const void *buffer, size_t size, void *ctx);

    bool (*is_empty)(void *ctx);

    bool (*is_full)(void *ctx);

    size_t (*available)(void *ctx);

    void (*consumed)(size_t nbytes, void *ctx);

    /**
     * The client asks to read more data.  The filter shall call
     * filtered_socket_internal_data() again.
     */
    bool (*read)(bool expect_more, void *ctx);

    /**
     * The client asks to write data to the socket.  The filter
     * processes it, and may then call
     * filtered_socket_internal_write().
     */
    ssize_t (*write)(const void *data, size_t length, void *ctx);

    /**
     * The client is willing to read, but does not expect it yet.  The
     * filter processes the call, and may then call
     * filtered_socket_internal_schedule_read().
     */
    void (*schedule_read)(bool expect_more, const struct timeval *timeout,
                          void *ctx);

    /**
     * The client wants to be called back as soon as writing becomes
     * possible.  The filter processes the call, and may then call
     * filtered_socket_internal_schedule_write().
     */
    void (*schedule_write)(void *ctx);

    /**
     * The client is not anymore interested in writing.  The filter
     * processes the call, and may then call
     * filtered_socket_internal_unschedule_write().
     */
    void (*unschedule_write)(void *ctx);

    /**
     * The underlying socket is ready for writing.  The filter may try
     * calling filtered_socket_internal_write() again.
     *
     * This method must not destroy the socket.  If an error occurs,
     * it shall return false.
     */
    bool (*internal_write)(void *ctx);

    /**
     * Called after the socket has been closed/abandoned (either by
     * the peer or locally).  The filter shall update its internal
     * state, but not do any invasive actions.
     */
    void (*closed)(void *ctx);

    bool (*remaining)(size_t remaining, void *ctx);

    /**
     * The buffered_socket has run empty after the socket has been
     * closed.  The filter may call filtered_socket_invoke_end() as
     * soon as all its buffers have been consumed.
     */
    void (*end)(void *ctx);

    void (*close)(void *ctx);
};
