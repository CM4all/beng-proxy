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

#include "SliceAllocation.hxx"
#include "util/WritableBuffer.hxx"

#include <assert.h>
#include <stdint.h>

class SlicePool;
class SliceArea;

/**
 * A buffer allocated from a #SlicePool which the caller can append
 * data to, until it is full.  This is a simplified version of
 * #SliceFifoBuffer which cannot consume any data.
 */
class SliceBuffer {
    SliceAllocation allocation;

    size_t fill;

public:
    SliceBuffer() = default;

    explicit SliceBuffer(SliceAllocation &&src) noexcept
        :allocation(std::move(src)), fill(0) {}

    SliceBuffer(SliceBuffer &&) noexcept = default;

    ~SliceBuffer() noexcept {
        if (allocation.IsDefined())
            allocation.Free();
    }

    SliceBuffer &operator=(SliceBuffer &&) noexcept = default;

    SliceBuffer &operator=(SliceAllocation &&src) noexcept {
        allocation = std::move(src);
        fill = 0;
        return *this;
    }

    bool IsDefined() const noexcept {
        return allocation.IsDefined();
    }

    auto size() const noexcept {
        assert(IsDefined());

        return fill;
    }

    ConstBuffer<uint8_t> Read() noexcept {
        assert(IsDefined());

        return {(const uint8_t *)allocation.data, fill};
    }

    WritableBuffer<uint8_t> Write() noexcept {
        assert(IsDefined());

        return {(uint8_t *)allocation.data + fill, allocation.size - fill};
    }

    void Append(size_t n) noexcept {
        assert(IsDefined());

        fill += n;
    }

    SliceAllocation &&StealAllocation() noexcept {
        return std::move(allocation);
    }
};
