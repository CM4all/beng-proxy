/*
 * A wrapper that turns a growing_buffer into an istream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_gb.hxx"
#include "istream/istream_oo.hxx"
#include "istream/Bucket.hxx"
#include "growing_buffer.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Cast.hxx"

class GrowingBufferIstream final : public Istream {
    GrowingBufferReader reader;

    class Bucket final : public IstreamBucket {
    public:
        void Release(size_t consumed) override {
            auto &i = ContainerCast2(*this, &GrowingBufferIstream::bucket);
            i.ConsumeBucket(consumed);
        }
    };

    Bucket bucket;

public:
    GrowingBufferIstream(struct pool &p, const GrowingBuffer &_gb)
        :Istream(p), reader(_gb) {}

    /* virtual methods from class Istream */

    off_t _GetAvailable(gcc_unused bool partial) override {
        return reader.Available();
    }

    off_t _Skip(off_t _nbytes) override {
        size_t nbytes = _nbytes > off_t(reader.Available())
            ? reader.Available()
            : size_t(_nbytes);

        reader.Skip(nbytes);
        return nbytes;
    }

    void _Read() override {
        /* this loop is required to cross the buffer borders */
        while (true) {
            auto src = reader.Read();
            if (src.IsNull()) {
                assert(reader.IsEOF());
                DestroyEof();
                return;
            }

            assert(!reader.IsEOF());

            size_t nbytes = InvokeData(src.data, src.size);
            if (nbytes == 0)
                /* growing_buffer has been closed */
                return;

            reader.Consume(nbytes);
            if (nbytes < src.size)
                return;
        }
    }

    bool _FillBucketList(IstreamBucketList &list, GError **) override {
        auto r = reader.Read();
        if (!r.IsEmpty()) {
            bucket.Set(r);
            list.Push(bucket);

            // TODO: push multiple buckets
            if (reader.Available() > r.size)
                list.SetMore();
        }

        return true;
    }

private:
    void ConsumeBucket(size_t nbytes) {
        reader.Consume(nbytes);
        Consumed(nbytes);
    }
};

struct istream *
istream_gb_new(struct pool *pool, const GrowingBuffer *gb)
{
    assert(gb != nullptr);

    return NewIstream<GrowingBufferIstream>(*pool, *gb);
}
