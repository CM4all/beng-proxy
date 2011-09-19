/*
 * A auto-growing buffer you can write to.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_GROWING_BUFFER_H
#define __BENG_GROWING_BUFFER_H

#include <inline/compiler.h>

#include <stdbool.h>
#include <stddef.h>

struct pool;

struct growing_buffer_reader {
#ifndef NDEBUG
    const struct growing_buffer *growing_buffer;
#endif

    const struct buffer *buffer;
    size_t position;
};

struct growing_buffer *__attr_malloc
growing_buffer_new(struct pool *pool, size_t initial_size);

void *
growing_buffer_write(struct growing_buffer *gb, size_t length);

void
growing_buffer_write_buffer(struct growing_buffer *gb, const void *p, size_t length);

void
growing_buffer_write_string(struct growing_buffer *gb, const char *p);

void
growing_buffer_cat(struct growing_buffer *dest, struct growing_buffer *src);

/**
 * Returns the total size of the buffer.
 */
size_t
growing_buffer_size(const struct growing_buffer *gb);

void
growing_buffer_reader_init(struct growing_buffer_reader *reader,
                           const struct growing_buffer *gb);

/**
 * Update the reader object after data has been appended to the
 * underlying buffer.
 */
void
growing_buffer_reader_update(struct growing_buffer_reader *reader);

bool
growing_buffer_reader_eof(const struct growing_buffer_reader *reader);

size_t
growing_buffer_reader_available(const struct growing_buffer_reader *reader);

const void *
growing_buffer_reader_read(const struct growing_buffer_reader *reader,
                           size_t *length_r);

/**
 * Consume data returned by growing_buffer_reader_read().
 */
void
growing_buffer_reader_consume(struct growing_buffer_reader *reader,
                              size_t length);

/**
 * Skip an arbitrary number of data bytes, which may span over
 * multiple internal buffers.
 */
void
growing_buffer_reader_skip(struct growing_buffer_reader *reader,
                           size_t length);

/**
 * Duplicates the whole buffer (including all chunks) to one
 * contiguous buffer.
 */
void *
growing_buffer_dup(const struct growing_buffer *gb, struct pool *pool,
                   size_t *length_r);

/**
 * Similar to growing_buffer_dup(), but concatenates two
 * #growing_buffer objects.
 */
void *
growing_buffer_dup2(const struct growing_buffer *a,
                    const struct growing_buffer *b,
                    struct pool *pool, size_t *length_r);

#endif
