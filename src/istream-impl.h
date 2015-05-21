/*
 * Asynchronous input stream API, constructors of istream
 * implementations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ISTREAM_IMPL_H
#define __BENG_ISTREAM_IMPL_H

#include <stdbool.h>

struct pool;
struct async_operation;
struct cache;
struct cache_item;
struct stopwatch;

#ifdef __cplusplus
extern "C" {
#endif

struct istream *
istream_zero_new(struct pool *pool);

struct istream *
istream_fail_new(struct pool *pool, GError *error);

struct istream *
istream_inject_new(struct pool *pool, struct istream *input);

void
istream_inject_fault(struct istream *i_fault, GError *error);

struct istream *
istream_later_new(struct pool *pool, struct istream *input);

struct istream *gcc_malloc
istream_memory_new(struct pool *pool, const void *data, size_t length);

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
istream_four_new(struct pool *pool, struct istream *input);

struct istream *
istream_trace_new(struct pool *pool, struct istream *input);

/**
 * @param authoritative is the specified size authoritative?
 */
struct istream *
istream_head_new(struct pool *pool, struct istream *input, size_t size,
                 bool authoritative);

struct istream *
istream_iconv_new(struct pool *pool, struct istream *input,
                  const char *tocode, const char *fromcode);

struct istream *
istream_unlock_new(struct pool *pool, struct istream *input,
                   struct cache *cache, struct cache_item *item);

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

#ifdef __cplusplus
}
#endif

#endif
