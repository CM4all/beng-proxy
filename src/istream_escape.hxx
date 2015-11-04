/*
 * An istream filter that escapes the data.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_ESCAPE_HXX
#define BENG_PROXY_ISTREAM_ESCAPE_HXX

struct pool;
class Istream;
struct escape_class;

Istream *
istream_escape_new(struct pool &pool, Istream &input,
                   const struct escape_class &cls);

#endif
