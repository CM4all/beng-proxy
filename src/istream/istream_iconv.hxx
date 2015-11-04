/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_ICONV_HXX
#define BENG_PROXY_ISTREAM_ICONV_HXX

struct pool;
class Istream;

Istream *
istream_iconv_new(struct pool *pool, Istream &input,
                  const char *tocode, const char *fromcode);

#endif
