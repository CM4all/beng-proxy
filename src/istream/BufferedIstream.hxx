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

#include <exception>

struct pool;
class EventLoop;
class Stock;
class CancellablePointer;
class UnusedIstreamPtr;

/**
 * Handler interface for NewBufferedIstream().
 */
class BufferedIstreamHandler {
public:
    virtual void OnBufferedIstreamReady(UnusedIstreamPtr i) noexcept = 0;
    virtual void OnBufferedIstreamError(std::exception_ptr e) noexcept = 0;
};

/**
 * This asynchronous class registers itself as #IstreamHandler for the
 * given #Istream and waits until data becomes available.  As soon as
 * data arrives, it is collected in a pipe or in a buffer.  When the
 * buffer is full, it invokes the #BufferedIstreamHandler and gives it
 * a new #Istream with buffered data plus remaining data.
 *
 * This class can be useful to postpone invoking filter processes
 * until there is really data, to avoid blocking filter processes
 * while there is nothing to do yet.
 */
void
NewBufferedIstream(struct pool &pool, EventLoop &event_loop,
                   Stock *pipe_stock,
                   BufferedIstreamHandler &handler,
                   UnusedIstreamPtr i,
                   CancellablePointer &cancel_ptr) noexcept;
