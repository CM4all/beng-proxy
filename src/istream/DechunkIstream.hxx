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

#pragma once

struct pool;
class EventLoop;
class UnusedIstreamPtr;

class DechunkHandler {
public:
    /**
     * Called as soon as the dechunker has seen the end chunk in data
     * provided by the input.  At this time, the end chunk may not yet
     * ready to be processed, but it's an indicator that input's
     * underlying socket is done.
     */
    virtual void OnDechunkEndSeen() noexcept = 0;

    /**
     * Called after the end chunk has been consumed from the input,
     * right before calling IstreamHandler::OnEof().
     *
     * @return false if the caller shall close its input
     */
    virtual bool OnDechunkEnd() noexcept = 0;
};

/**
 * This istream filter removes HTTP chunking.
 *
 * @param eof_callback a callback function which is called when the
 * last chunk is being consumed; note that this occurs inside the
 * data() callback, so the istream doesn't know yet how much is
 * consumed
 */
UnusedIstreamPtr
istream_dechunk_new(struct pool &pool, UnusedIstreamPtr input,
                    EventLoop &event_loop,
                    DechunkHandler &dechunk_handler) noexcept;

/**
 * Check if the parameter is an istream_dechunk, and if so, switch to
 * "verbatim" mode and return true.  May only be called on a pristine
 * object.
 *
 * In "verbatim" mode, this istream's output is still chunked, but
 * verified, and its end-of-file is detected.  This is useful when we
 * need to output chunked data (e.g. proxying to another client).
 */
bool
istream_dechunk_check_verbatim(UnusedIstreamPtr &i) noexcept;
