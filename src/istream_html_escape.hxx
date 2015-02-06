/*
 * istream implementation which blocks indefinitely until closed.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_HTML_ESCAPE_HXX
#define BENG_PROXY_ISTREAM_HTML_ESCAPE_HXX

struct pool;
struct istream;

struct istream *
istream_block_new(struct pool &pool);

#endif
