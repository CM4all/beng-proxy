/*
 * A auto-growing buffer you can write to.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_GROWING_BUFFER_HXX
#define BENG_PROXY_GROWING_BUFFER_HXX

#include <inline/compiler.h>

#include <stddef.h>

struct pool;
template<typename T> struct ConstBuffer;
template<typename T> struct WritableBuffer;
class IstreamBucketList;

class GrowingBuffer {
    friend class GrowingBufferReader;

    struct Buffer {
        Buffer *next = nullptr;
        const size_t size;
        size_t fill = 0;
        char data[sizeof(size_t)];

        explicit Buffer(size_t _size)
            :size(_size) {}

        static Buffer *New(struct pool &pool, size_t size);

        bool IsFull() const {
            return fill == size;
        }

        WritableBuffer<void> Write();
        size_t WriteSome(ConstBuffer<void> src);
    };

    struct pool &pool;

    const size_t default_size;

    Buffer *head = nullptr, *tail = nullptr;

public:
    GrowingBuffer(struct pool &_pool, size_t _default_size);

    GrowingBuffer(GrowingBuffer &&src)
        :pool(src.pool),
         default_size(src.default_size),
         head(src.head), tail(src.tail) {
        src.Release();
    }

    bool IsEmpty() const {
        return head == nullptr;
    }

    void Clear() {
        Release();
    }

    /**
     * Release the buffer list, which may now be owned by somebody
     * else.
     */
    void Release() {
        head = tail = nullptr;
    }

    void *Write(size_t length);

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

private:
    void AppendBuffer(Buffer &buffer);
    Buffer &AppendBuffer(size_t min_size);

    void CopyTo(void *dest) const;
};

class GrowingBufferReader {
#ifndef NDEBUG
    const GrowingBuffer *growing_buffer;
#endif

    const GrowingBuffer::Buffer *buffer;
    size_t position;

public:
    explicit GrowingBufferReader(const GrowingBuffer &gb);

    /**
     * Update the reader object after data has been appended to the
     * underlying buffer.
     */
    void Update(const GrowingBuffer &gb);

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
     * Peek data from the buffer following the current one.
     */
    ConstBuffer<void> PeekNext() const;

    /**
     * Skip an arbitrary number of data bytes, which may span over
     * multiple internal buffers.
     */
    void Skip(size_t length);

    void FillBucketList(IstreamBucketList &list) const;
    size_t ConsumeBucketList(size_t nbytes);
};

GrowingBuffer *gcc_malloc
growing_buffer_new(struct pool *pool, size_t initial_size);

#endif
