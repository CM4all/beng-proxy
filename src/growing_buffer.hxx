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
struct GrowingBuffer;
template<typename T> struct ConstBuffer;

class GrowingBufferReader {
#ifndef NDEBUG
    const GrowingBuffer *growing_buffer;
#endif

    const struct buffer *buffer;
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
     * Skip an arbitrary number of data bytes, which may span over
     * multiple internal buffers.
     */
    void Skip(size_t length);
};

GrowingBuffer *gcc_malloc
growing_buffer_new(struct pool *pool, size_t initial_size);

void *
growing_buffer_write(GrowingBuffer *gb, size_t length);

void
growing_buffer_write_buffer(GrowingBuffer *gb, const void *p, size_t length);

void
growing_buffer_write_string(GrowingBuffer *gb, const char *p);

void
growing_buffer_cat(GrowingBuffer *dest, GrowingBuffer *src);

/**
 * Returns the total size of the buffer.
 */
size_t
growing_buffer_size(const GrowingBuffer *gb);

/**
 * Duplicates the whole buffer (including all chunks) to one
 * contiguous buffer.
 */
void *
growing_buffer_dup(const GrowingBuffer *gb, struct pool *pool,
                   size_t *length_r);

#endif
