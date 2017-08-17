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

#ifndef BENG_PROXY_GROWING_BUFFER_HXX
#define BENG_PROXY_GROWING_BUFFER_HXX

#include "DefaultChunkAllocator.hxx"

#include "util/Compiler.h"

#include <utility>

#include <stddef.h>

template<typename T> struct ConstBuffer;
template<typename T> struct WritableBuffer;
class IstreamBucketList;

/**
 * An auto-growing buffer you can write to.
 */
class GrowingBuffer {
    friend class GrowingBufferReader;

    struct Buffer;

    struct BufferPtr {
        Buffer *buffer = nullptr;
        DefaultChunkAllocator allocator;

        BufferPtr() = default;

        BufferPtr(BufferPtr &&src)
            :buffer(src.buffer), allocator(std::move(src.allocator)) {
            src.buffer = nullptr;
        }

        ~BufferPtr() {
            if (buffer != nullptr)
                Free();
        }

        BufferPtr &operator=(BufferPtr &&src) {
            using std::swap;
            swap(buffer, src.buffer);
            swap(allocator, src.allocator);
            return *this;
        }

        operator bool() const {
            return buffer != nullptr;
        }

        Buffer &Allocate();
        void Free();

        void Pop();

        const Buffer *get() const {
            return buffer;
        }

        Buffer *get() {
            return buffer;
        }

        const Buffer &operator*() const {
            return *buffer;
        }

        Buffer &operator*() {
            return *buffer;
        }

        const Buffer *operator->() const {
            return buffer;
        }

        Buffer *operator->() {
            return buffer;
        }
    };

    struct Buffer {
        BufferPtr next;

        const size_t size;
        size_t fill = 0;
        char data[sizeof(size_t)];

        explicit Buffer(size_t _size)
            :size(_size) {}

        bool IsFull() const {
            return fill == size;
        }

        WritableBuffer<void> Write();
        size_t WriteSome(ConstBuffer<void> src);
    };

    BufferPtr head;
    Buffer *tail = nullptr;

    size_t position = 0;

public:
    GrowingBuffer() = default;

    GrowingBuffer(GrowingBuffer &&src)
        :head(std::move(src.head)), tail(src.tail) {
        src.tail = nullptr;
    }

    bool IsEmpty() const {
        return tail == nullptr;
    }

    void Clear() {
        Release();
    }

    /**
     * Release the buffer list, which may now be owned by somebody
     * else.
     */
    void Release() {
        if (head)
            head.Free();
        tail = nullptr;
        position = 0;
    }

    void *Write(size_t length);

    size_t WriteSome(const void *p, size_t length);
    void Write(const void *p, size_t length);

    void Write(const char *p);

    void AppendMoveFrom(GrowingBuffer &&src);

    /**
     * Returns the total size of the buffer.
     */
    gcc_pure
    size_t GetSize() const;

    /**
     * Duplicates the whole buffer (including all chunks) to one
     * contiguous buffer.
     */
    WritableBuffer<void> Dup(struct pool &pool) const;

    gcc_pure
    ConstBuffer<void> Read() const;

    /**
     * Skip an arbitrary number of data bytes, which may span over
     * multiple internal buffers.
     */
    void Skip(size_t length);

    /**
     * Consume data returned by Read().
     */
    void Consume(size_t length);

private:
    Buffer &AppendBuffer();

    void CopyTo(void *dest) const;

    template<typename F>
    void ForEachBuffer(F &&f) const {
        const auto *i = head.get();
        if (i == nullptr)
            return;

        f({i->data + position, i->fill - position});

        while ((i = i->next.get()) != nullptr)
            f({i->data, i->fill});
    }
};

class GrowingBufferReader {
    GrowingBuffer::BufferPtr buffer;
    size_t position = 0;

public:
    explicit GrowingBufferReader(GrowingBuffer &&gb);

    gcc_pure
    bool IsEOF() const;

    gcc_pure
    size_t Available() const;

    gcc_pure
    ConstBuffer<void> Read() const;

    /**
     * Consume data returned by Read().
     */
    void Consume(size_t length);

    /**
     * Skip an arbitrary number of data bytes, which may span over
     * multiple internal buffers.
     */
    void Skip(size_t length);

    void FillBucketList(IstreamBucketList &list) const;
    size_t ConsumeBucketList(size_t nbytes);

private:
    template<typename F>
    void ForEachBuffer(F &&f) const {
        const auto *i = buffer.get();
        if (i == nullptr)
            return;

        f({i->data + position, i->fill - position});

        while ((i = i->next.get()) != nullptr)
            f({i->data, i->fill});
    }
};

#endif
