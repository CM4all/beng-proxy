/*
 * Write HTTP headers into a buffer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HEADER_WRITER_HXX
#define BENG_PROXY_HEADER_WRITER_HXX

struct pool;
struct strmap;
struct GrowingBuffer;

/**
 * Begin writing a header line.  After this, you may write the value.
 * Call header_write_finish() when you're done.
 */
void
header_write_begin(GrowingBuffer *gb, const char *name);

/**
 * Finish the current header line.
 *
 * @see header_write_begin().
 */
void
header_write_finish(GrowingBuffer *gb);

void
header_write(GrowingBuffer *gb, const char *key, const char *value);

void
headers_copy_one(const struct strmap *in, GrowingBuffer *out,
                 const char *key);

void
headers_copy(const struct strmap *in, GrowingBuffer *out,
             const char *const* keys);

void
headers_copy_all(const struct strmap *in, GrowingBuffer *out);

/**
 * Like headers_copy_all(), but doesn't copy hop-by-hop headers.
 */
void
headers_copy_most(const struct strmap *in, GrowingBuffer *out);

GrowingBuffer *
headers_dup(struct pool *pool, const struct strmap *in);

#endif
