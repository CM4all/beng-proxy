/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_ICONV_HXX
#define BENG_PROXY_ISTREAM_ICONV_HXX

struct pool;
struct istream;

struct istream *
istream_iconv_new(struct pool *pool, struct istream *input,
                  const char *tocode, const char *fromcode);

#endif
