/*
 * Asynchronous input stream API, utilities for istream
 * implementations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ISTREAM_BUCKET_HXX
#define ISTREAM_BUCKET_HXX

#include "util/ConstBuffer.hxx"

#include <boost/intrusive/slist.hpp>

class IstreamBucket
    : public boost::intrusive::slist_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {
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
    typedef boost::intrusive::slist<IstreamBucket,
                                    boost::intrusive::constant_time_size<false>> List;

    List list;
    List::iterator tail;

    bool more = false;

public:
    IstreamBucketList():tail(list.before_begin()) {}

    IstreamBucketList(const IstreamBucketList &) = delete;
    IstreamBucketList &operator=(const IstreamBucketList &) = delete;

    void SetMore() {
        more = true;
    }

    bool HasMore() const {
        return more;
    }

    bool IsEmpty() const {
        return list.empty();
    }

    void Clear() {
        list.clear();
    }

    void Push(IstreamBucket &bucket) {
        tail = list.insert_after(tail, bucket);
    }

    IstreamBucket &Pop() {
        auto &b = list.front();
        list.pop_front();
        return b;
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

        while (!src.IsEmpty()) {
            auto &bucket = src.Pop();
            if (bucket.GetType() != IstreamBucket::Type::BUFFER)
                max_size = 0;

            if (max_size == 0) {
                SetMore();
                break;
            }

            auto buffer = bucket.GetBuffer();
            if (buffer.size > max_size) {
                buffer.size = max_size;
                bucket.Set(buffer);
                SetMore();
            }

            Push(bucket);
            max_size -= buffer.size;
        }
    }
};

#endif
