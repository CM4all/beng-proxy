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

struct GrowingBuffer {
    struct Buffer {
        Buffer *next;
        size_t length;
        char data[sizeof(size_t)];

        static Buffer *New(struct pool &pool, size_t size);
    };

    struct pool &pool;

#ifndef NDEBUG
    const size_t initial_size;
#endif

    size_t size;
    Buffer *current, *tail, first;

    GrowingBuffer(struct pool &_pool, size_t _initial_size);

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
    void Update();

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
};

GrowingBuffer *gcc_malloc
growing_buffer_new(struct pool *pool, size_t initial_size);

#endif
