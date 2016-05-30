/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_DEFLATE_HXX
#define BENG_PROXY_ISTREAM_DEFLATE_HXX

struct pool;
class Istream;
class EventLoop;

/**
 * @param gzip use the gzip format instead of the zlib format?
 */
Istream *
istream_deflate_new(struct pool &pool, Istream &input, EventLoop &event_loop,
                    bool gzip=false);

#endif
