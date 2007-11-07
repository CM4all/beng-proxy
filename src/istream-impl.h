/*
 * Asynchronous input stream API, constructors of istream
 * implementations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ISTREAM_IMPL_H
#define __BENG_ISTREAM_IMPL_H

istream_t
istream_null_new(pool_t pool);

istream_t
istream_fail_new(pool_t pool);

istream_t attr_malloc
istream_memory_new(pool_t pool, const void *data, size_t length);

istream_t attr_malloc
istream_string_new(pool_t pool, const char *s);

istream_t attr_malloc
istream_file_new(pool_t pool, const char *path, off_t length);

#ifdef __linux
istream_t
istream_pipe_new(pool_t pool, istream_t input);
#endif

istream_t
istream_chunked_new(pool_t pool, istream_t input);

istream_t
istream_dechunk_new(pool_t pool, istream_t input,
                    void (*eof_callback)(void *ctx), void *ctx);

istream_t
istream_cat_new(pool_t pool, ...);

istream_t
istream_delayed_new(pool_t pool, void (*abort_callback)(void *ctx),
                    void *callback_ctx);

void
istream_delayed_set(istream_t istream_delayed, istream_t input);

istream_t
istream_hold_new(pool_t pool, istream_t input);

istream_t
istream_deflate_new(pool_t pool, istream_t input);

istream_t
istream_subst_new(pool_t pool, istream_t input,
                  const char *a, const char *b);

istream_t
istream_byte_new(pool_t pool, istream_t input);

istream_t
istream_head_new(pool_t pool, istream_t input, size_t size);

#endif
