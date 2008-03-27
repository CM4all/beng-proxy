/*
 * Asynchronous input stream API, constructors of istream
 * implementations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ISTREAM_IMPL_H
#define __BENG_ISTREAM_IMPL_H

struct async_operation;

istream_t
istream_null_new(pool_t pool);

istream_t
istream_block_new(pool_t pool);

istream_t
istream_fail_new(pool_t pool);

istream_t
istream_later_new(pool_t pool, istream_t input);

istream_t __attr_malloc
istream_memory_new(pool_t pool, const void *data, size_t length);

istream_t __attr_malloc
istream_string_new(pool_t pool, const char *s);

istream_t __attr_malloc
istream_file_new(pool_t pool, const char *path, off_t length);

#ifdef __linux
istream_t
istream_pipe_new(pool_t pool, istream_t input);
#endif

istream_t
istream_chunked_new(pool_t pool, istream_t input);

istream_t
istream_dechunk_new(pool_t pool, istream_t input);

int
istream_dechunk_eof(istream_t istream);

istream_t
istream_cat_new(pool_t pool, ...);

istream_t
istream_delayed_new(pool_t pool, struct async_operation *async);

struct async_operation_ref *
istream_delayed_async(istream_t i_delayed);

void
istream_delayed_set(istream_t istream_delayed, istream_t input);

void
istream_delayed_set_eof(istream_t istream_delayed);

istream_t
istream_hold_new(pool_t pool, istream_t input);

istream_t
istream_deflate_new(pool_t pool, istream_t input);

istream_t
istream_subst_new(pool_t pool, istream_t input);

int
istream_subst_add(istream_t istream, const char *a, const char *b);

istream_t
istream_byte_new(pool_t pool, istream_t input);

istream_t
istream_trace_new(pool_t pool, istream_t input);

istream_t
istream_head_new(pool_t pool, istream_t input, size_t size);

istream_t
istream_tee_new(pool_t pool, istream_t input, int fragile);

istream_t
istream_tee_second(istream_t istream);

istream_t
istream_replace_new(pool_t pool, istream_t input, int quiet);

void
istream_replace_add(istream_t istream, off_t start, off_t end,
                    istream_t contents);

void
istream_replace_finish(istream_t istream);

#endif
