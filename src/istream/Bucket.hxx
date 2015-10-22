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

    virtual void Release(size_t consumed) = 0;
};


class IstreamBucketList {
    typedef boost::intrusive::slist<IstreamBucket,
                                    boost::intrusive::constant_time_size<false>> List;

    List list;
    List::iterator tail;

    bool more = false;

public:
    IstreamBucketList():tail(list.before_begin()) {}

    ~IstreamBucketList() {
        assert(list.empty());
    }

    IstreamBucketList(const IstreamBucketList &) = delete;
    IstreamBucketList &operator=(const IstreamBucketList &) = delete;

    void SetMore() {
        assert(!more);

        more = true;
    }

    bool HasMore() const {
        return more;
    }

    bool IsEmpty() const {
        return list.empty();
    }

    void Clear() {
        list.clear_and_dispose([](IstreamBucket *b){
                b->Release(0);
            });
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

    /**
     * Release all buckets, consuming bytes from BUFFER buckets.
     *
     * @return true if all buckets have been consumed completely, and
     * the stream has ended
     */
    bool ReleaseBuffers(size_t consumed) {
        bool result = !HasMore();
        list.clear_and_dispose([&](IstreamBucket *b){
                size_t n = 0;
                if (b->GetType() != IstreamBucket::Type::BUFFER) {
                    result = false;
                } else {
                    n = b->GetBuffer().size;
                    if (consumed < n) {
                        n = consumed;
                        result = false;
                    }
                }

                b->Release(n);
                consumed -= n;
            });
        return result;
    }
};

#endif
