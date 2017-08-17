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

#ifndef ISTREAM_BUCKET_HXX
#define ISTREAM_BUCKET_HXX

#include "util/ConstBuffer.hxx"
#include "util/StaticArray.hxx"

class IstreamBucket {
public:
    enum class Type {
        BUFFER,
    };

private:
    Type type;

    union {
        ConstBuffer<void> buffer;
    };

public:
    IstreamBucket() = default;

    Type GetType() const {
        return type;
    }

    ConstBuffer<void> GetBuffer() const {
        assert(type == Type::BUFFER);

        return buffer;
    }

    void Set(ConstBuffer<void> _buffer) {
        type = Type::BUFFER;
        buffer = _buffer;
    }
};

class IstreamBucketList {
    typedef StaticArray<IstreamBucket, 64> List;
    List list;

    bool more = false;

public:
    IstreamBucketList() = default;

    IstreamBucketList(const IstreamBucketList &) = delete;
    IstreamBucketList &operator=(const IstreamBucketList &) = delete;

    void SetMore(bool _more=true) {
        more = _more;
    }

    bool HasMore() const {
        return more;
    }

    bool IsEmpty() const {
        return list.empty();
    }

    bool IsFull() const {
        return list.full();
    }

    void Clear() {
        list.clear();
    }

    void Push(const IstreamBucket &bucket) {
        if (IsFull()) {
            SetMore();
            return;
        }

        list.append(bucket);
    }

    void Push(ConstBuffer<void> buffer) {
        if (IsFull()) {
            SetMore();
            return;
        }

        list.append().Set(buffer);
    }

    List::const_iterator begin() const {
        return list.begin();
    }

    List::const_iterator end() const {
        return list.end();
    }

    gcc_pure
    bool HasNonBuffer() const {
        for (const auto &bucket : list)
            if (bucket.GetType() != IstreamBucket::Type::BUFFER)
                return true;
        return false;
    }

    gcc_pure
    size_t GetTotalBufferSize() const {
        size_t size = 0;
        for (const auto &bucket : list)
            if (bucket.GetType() == IstreamBucket::Type::BUFFER)
                size += bucket.GetBuffer().size;
        return size;
    }

    gcc_pure
    bool IsDepleted(size_t consumed) const {
        return !HasMore() && consumed == GetTotalBufferSize();
    }

    void SpliceBuffersFrom(IstreamBucketList &src, size_t max_size) {
        if (src.HasMore())
            SetMore();

        for (const auto &bucket : src) {
            if (bucket.GetType() != IstreamBucket::Type::BUFFER)
                max_size = 0;

            if (max_size == 0) {
                SetMore();
                break;
            }

            auto buffer = bucket.GetBuffer();
            if (buffer.size > max_size) {
                buffer.size = max_size;
                SetMore();
            }

            Push(buffer);
            max_size -= buffer.size;
        }
    }

    /**
     * Move buffer buckets from the given list, stopping at the first
     * no-buffer bucket.
     *
     * @return the number of bytes in all moved buffers
     */
    size_t SpliceBuffersFrom(IstreamBucketList &src) {
        if (src.HasMore())
            SetMore();

        size_t total_size = 0;
        for (const auto &bucket : src) {
            if (bucket.GetType() != IstreamBucket::Type::BUFFER) {
                SetMore();
                break;
            }

            auto buffer = bucket.GetBuffer();
            Push(bucket.GetBuffer());
            total_size += buffer.size;
        }

        return total_size;
    }
};

#endif
