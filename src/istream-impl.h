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

struct pool;
struct async_operation;
struct stock;
struct cache;
struct cache_item;
struct stopwatch;

struct istream *
istream_null_new(struct pool *pool);

struct istream *
istream_zero_new(struct pool *pool);

struct istream *
istream_block_new(struct pool *pool);

struct istream *
istream_fail_new(struct pool *pool, GError *error);

struct istream *
istream_inject_new(struct pool *pool, struct istream *input);

void
istream_inject_fault(struct istream *i_fault, GError *error);

struct istream *
istream_catch_new(struct pool *pool, struct istream *input,
                  GError *(*callback)(GError *error, void *ctx), void *ctx);

struct istream *
istream_later_new(struct pool *pool, struct istream *input);

struct istream *gcc_malloc
istream_memory_new(struct pool *pool, const void *data, size_t length);

struct istream *gcc_malloc
istream_string_new(struct pool *pool, const char *s);

#ifdef __linux
struct istream *
istream_pipe_new(struct pool *pool, struct istream *input, struct stock *pipe_stock);
#endif

struct istream *
istream_chunked_new(struct pool *pool, struct istream *input);

/**
 * @param eof_callback a callback function which is called when the
 * last chunk is being consumed; note that this occurs inside the
 * data() callback, so the istream doesn't know yet how much is
 * consumed
 */
struct istream *
istream_dechunk_new(struct pool *pool, struct istream *input,
                    void (*eof_callback)(void *ctx), void *callback_ctx);

/**
 * @param request_id the FastCGI request id in network byte order
 */
struct istream *
istream_fcgi_new(struct pool *pool, struct istream *input, uint16_t request_id);

struct istream *
istream_cat_new(struct pool *pool, ...);

struct istream *
istream_delayed_new(struct pool *pool);

struct async_operation_ref *
istream_delayed_async_ref(struct istream *i_delayed);

void
istream_delayed_set(struct istream *istream_delayed, struct istream *input);

void
istream_delayed_set_eof(struct istream *istream_delayed);

/**
 * Injects a failure, to be called instead of istream_delayed_set().
 */
void
istream_delayed_set_abort(struct istream *istream_delayed, GError *error);

struct istream *
istream_hold_new(struct pool *pool, struct istream *input);

struct istream *
istream_optional_new(struct pool *pool, struct istream *input);

/**
 * Allows the istream to resume, but does not trigger reading.
 */
void
istream_optional_resume(struct istream *istream);

/**
 * Discard the stream contents.
 */
void
istream_optional_discard(struct istream *istream);

struct istream *
istream_html_escape_new(struct pool *pool, struct istream *input);

struct istream *
istream_deflate_new(struct pool *pool, struct istream *input);

struct istream *
istream_subst_new(struct pool *pool, struct istream *input);

bool
istream_subst_add_n(struct istream *istream, const char *a,
                    const char *b, size_t b_length);

bool
istream_subst_add(struct istream *istream, const char *a, const char *b);

struct istream *
istream_byte_new(struct pool *pool, struct istream *input);

struct istream *
istream_four_new(struct pool *pool, struct istream *input);

struct istream *
istream_trace_new(struct pool *pool, struct istream *input);

struct istream *
istream_head_new(struct pool *pool, struct istream *input, size_t size);

/**
 * Create two new streams fed from one input.
 *
 * @param input the istream which is duplicated
 * @param first_weak if true, closes the whole object if only the
 * first output remains
 * @param second_weak if true, closes the whole object if only the
 * second output remains
 */
struct istream *
istream_tee_new(struct pool *pool, struct istream *input,
                bool first_weak, bool second_weak);

struct istream *
istream_tee_second(struct istream *istream);

struct istream *
istream_iconv_new(struct pool *pool, struct istream *input,
                  const char *tocode, const char *fromcode);

struct istream *
istream_socketpair_new(struct pool *pool, struct istream *input, int *fd_r);

struct istream *
istream_unlock_new(struct pool *pool, struct istream *input,
                   struct cache *cache, struct cache_item *item);

struct istream *
istream_ajp_body_new(struct pool *pool, struct istream *input);

void
istream_ajp_body_request(struct istream *istream, size_t length);

#ifdef ENABLE_STOPWATCH

struct istream *
istream_stopwatch_new(struct pool *pool, struct istream *input,
                      struct stopwatch *_stopwatch);

#else /* !ENABLE_STOPWATCH */

static inline struct istream *
istream_stopwatch_new(struct pool *pool, struct istream *input,
                      struct stopwatch *_stopwatch)
{
    (void)pool;
    (void)_stopwatch;

    return input;
}

#endif /* !ENABLE_STOPWATCH */

#endif
