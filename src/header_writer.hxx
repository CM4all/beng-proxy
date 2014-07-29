/*
 * Write HTTP headers into a buffer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HEADER_WRITER_HXX
#define BENG_PROXY_HEADER_WRITER_HXX

struct pool;
struct strmap;
struct growing_buffer;

/**
 * Begin writing a header line.  After this, you may write the value.
 * Call header_write_finish() when you're done.
 */
void
header_write_begin(struct growing_buffer *gb, const char *name);

/**
 * Finish the current header line.
 *
 * @see header_write_begin().
 */
void
header_write_finish(struct growing_buffer *gb);

void
header_write(struct growing_buffer *gb, const char *key, const char *value);

void
headers_copy_one(const struct strmap *in, struct growing_buffer *out,
                 const char *key);

void
headers_copy(const struct strmap *in, struct growing_buffer *out,
             const char *const* keys);

void
headers_copy_all(const struct strmap *in, struct growing_buffer *out);

/**
 * Like headers_copy_all(), but doesn't copy hop-by-hop headers.
 */
void
headers_copy_most(const struct strmap *in, struct growing_buffer *out);

struct growing_buffer *
headers_dup(struct pool *pool, const struct strmap *in);

#endif
