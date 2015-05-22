/*
 * Asynchronous input stream API, constructors of istream
 * implementations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_ISTREAM_IMPL_H
#define __BENG_ISTREAM_IMPL_H

struct pool;

#ifdef __cplusplus
extern "C" {
#endif

struct istream *
istream_iconv_new(struct pool *pool, struct istream *input,
                  const char *tocode, const char *fromcode);

#ifdef __cplusplus
}
#endif

#endif
