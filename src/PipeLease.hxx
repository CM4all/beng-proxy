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

#include "io/FileDescriptor.hxx"

#include <utility>

class Stock;
class StockItem;

class PipeLease {
    Stock *stock;
    StockItem *item = nullptr;
    FileDescriptor fds[2]{FileDescriptor::Undefined(), FileDescriptor::Undefined()};

public:
    explicit PipeLease(Stock *_stock) noexcept
        :stock(_stock) {}

    PipeLease(PipeLease &&src) noexcept
        :stock(src.stock), item(std::exchange(src.item, nullptr)),
         fds(src.fds) {
        for (auto &fd : src.fds)
            fd.SetUndefined();
    }

    PipeLease &operator=(PipeLease &&src) noexcept {
        std::swap(stock, src.stock);
        std::swap(item, src.item);
        std::swap(fds, src.fds);
        return *this;
    }

    bool IsDefined() const noexcept {
        return fds[0].IsDefined();
    }

    /**
     * Throws on error.
     */
    void Create(struct pool &pool);

    /**
     * Ensure that there is a pipe.
     *
     * Throws on error.
     */
    void EnsureCreated(struct pool &pool) {
        if (!IsDefined())
            Create(pool);
    }

    void Release(bool reuse) noexcept;

    void ReleaseIfStock() noexcept {
        if (item != nullptr)
            Release(true);
    }

    void CloseWriteIfNotStock() noexcept {
        if (item == nullptr && fds[1].IsDefined())
            fds[1].Close();
    }

    FileDescriptor GetReadFd() noexcept {
        return fds[0];
    }

    FileDescriptor GetWriteFd() noexcept {
        return fds[1];
    }
};
