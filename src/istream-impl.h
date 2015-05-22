/*
 * Asynchronous input stream API, constructors of istream
 * implementations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ISTREAM_IMPL_H
#define __BENG_ISTREAM_IMPL_H

struct pool;
struct async_operation;
struct cache;
struct cache_item;

#ifdef __cplusplus
extern "C" {
#endif

struct istream *
istream_html_escape_new(struct pool *pool, struct istream *input);

struct istream *
istream_four_new(struct pool *pool, struct istream *input);

struct istream *
istream_trace_new(struct pool *pool, struct istream *input);

struct istream *
istream_iconv_new(struct pool *pool, struct istream *input,
                  const char *tocode, const char *fromcode);

struct istream *
istream_unlock_new(struct pool *pool, struct istream *input,
                   struct cache *cache, struct cache_item *item);

#ifdef __cplusplus
}
#endif

#endif
