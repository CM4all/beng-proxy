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

#include "SocketFilter.hxx"

/**
 * A module for #FilteredSocket that does not filter anything.  It
 * passes data as-is.  It is meant for debugging.
 */
class NopSocketFilter : public SocketFilter {
    FilteredSocket *socket;

public:
    /* virtual methods from SocketFilter */
    void Init(FilteredSocket &_socket) noexcept override {
        socket = &_socket;
    }

    BufferedResult OnData() noexcept override;
    bool IsEmpty() const noexcept override;
    bool IsFull() const noexcept override;
    size_t GetAvailable() const noexcept override;
    WritableBuffer<void> ReadBuffer() noexcept override;
    void Consumed(size_t nbytes) noexcept override;
    bool Read(bool expect_more) noexcept override;
    ssize_t Write(const void *data, size_t length) noexcept override;
    void ScheduleRead(bool expect_more,
                      const struct timeval *timeout) noexcept override;
    void ScheduleWrite() noexcept override;
    void UnscheduleWrite() noexcept override;
    bool InternalWrite() noexcept override;
    bool OnRemaining(size_t remaining) noexcept override;
    void OnEnd() noexcept override;
    void Close() noexcept override;
};
