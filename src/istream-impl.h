/*
 * Asynchronous input stream API, constructors of istream
 * implementations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ISTREAM_IMPL_H
#define __BENG_ISTREAM_IMPL_H

#include <stdbool.h>
#include <stdint.h>

struct stat;
struct async_operation;
struct stock;
struct cache;
struct cache_item;
struct stopwatch;

istream_t
istream_null_new(pool_t pool);

istream_t
istream_zero_new(pool_t pool);

istream_t
istream_block_new(pool_t pool);

istream_t
istream_fail_new(pool_t pool);

istream_t
istream_catch_new(pool_t pool, istream_t input);

istream_t
istream_later_new(pool_t pool, istream_t input);

istream_t __attr_malloc
istream_memory_new(pool_t pool, const void *data, size_t length);

istream_t __attr_malloc
istream_string_new(pool_t pool, const char *s);

istream_t
istream_file_fd_new(pool_t pool, const char *path,
                    int fd, enum istream_direct fd_type, off_t length);

/**
 * Opens a file and stats it.
 */
istream_t
istream_file_stat_new(pool_t pool, const char *path, struct stat *st);

istream_t __attr_malloc
istream_file_new(pool_t pool, const char *path, off_t length);

int
istream_file_fd(istream_t istream);

#ifdef __linux
istream_t
istream_pipe_new(pool_t pool, istream_t input, struct stock *pipe_stock);
#endif

istream_t
istream_chunked_new(pool_t pool, istream_t input);

/**
 * @param eof_callback a callback function which is called when the
 * last chunk is being consumed; note that this occurs inside the
 * data() callback, so the istream doesn't know yet how much is
 * consumed
 */
istream_t
istream_dechunk_new(pool_t pool, istream_t input,
                    void (*eof_callback)(void *ctx), void *callback_ctx);

/**
 * @param request_id the FastCGI request id in network byte order
 */
istream_t
istream_fcgi_new(pool_t pool, istream_t input, uint16_t request_id);

istream_t
istream_cat_new(pool_t pool, ...);

istream_t
istream_delayed_new(pool_t pool);

struct async_operation_ref *
istream_delayed_async_ref(istream_t i_delayed);

void
istream_delayed_set(istream_t istream_delayed, istream_t input);

void
istream_delayed_set_eof(istream_t istream_delayed);

/**
 * Injects a failure, to be called instead of istream_delayed_set().
 */
void
istream_delayed_set_abort(istream_t istream_delayed);

istream_t
istream_hold_new(pool_t pool, istream_t input);

istream_t
istream_optional_new(pool_t pool, istream_t input);

/**
 * Allows the istream to resume, but does not trigger reading.
 */
void
istream_optional_resume(istream_t istream);

/**
 * Discard the stream contents.
 */
void
istream_optional_discard(istream_t istream);

istream_t
istream_html_escape_new(pool_t pool, istream_t input);

istream_t
istream_deflate_new(pool_t pool, istream_t input);

istream_t
istream_subst_new(pool_t pool, istream_t input);

bool
istream_subst_add_n(istream_t istream, const char *a,
                    const char *b, size_t b_length);

bool
istream_subst_add(istream_t istream, const char *a, const char *b);

istream_t
istream_byte_new(pool_t pool, istream_t input);

istream_t
istream_four_new(pool_t pool, istream_t input);

istream_t
istream_trace_new(pool_t pool, istream_t input);

istream_t
istream_head_new(pool_t pool, istream_t input, size_t size);

/**
 * Create two new streams fed from one input.
 *
 * @param input the istream which is duplicated
 * @param first_weak if true, closes the whole object if only the
 * first output remains
 * @param second_weak if true, closes the whole object if only the
 * second output remains
 */
istream_t
istream_tee_new(pool_t pool, istream_t input,
                bool first_weak, bool second_weak);

istream_t
istream_tee_second(istream_t istream);

istream_t
istream_iconv_new(pool_t pool, istream_t input,
                  const char *tocode, const char *fromcode);

istream_t
istream_replace_new(pool_t pool, istream_t input);

void
istream_replace_add(istream_t istream, off_t start, off_t end,
                    istream_t contents);

void
istream_replace_finish(istream_t istream);

istream_t
istream_socketpair_new(pool_t pool, istream_t input, int *fd_r);

istream_t
istream_unlock_new(pool_t pool, istream_t input,
                   struct cache *cache, struct cache_item *item);

istream_t
istream_ajp_body_new(pool_t pool, istream_t input);

void
istream_ajp_body_request(istream_t istream, size_t length);

#ifdef ENABLE_STOPWATCH

istream_t
istream_stopwatch_new(pool_t pool, istream_t input,
                      struct stopwatch *_stopwatch);

#else /* !ENABLE_STOPWATCH */

static inline istream_t
istream_stopwatch_new(pool_t pool, istream_t input,
                      struct stopwatch *_stopwatch)
{
    (void)pool;
    (void)_stopwatch;

    return input;
}

#endif /* !ENABLE_STOPWATCH */

#endif
