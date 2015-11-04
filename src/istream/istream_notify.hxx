/*
 * An istream facade which invokes a callback when the istream is
 * finished / closed.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_NOTIFY_HXX
#define BENG_PROXY_ISTREAM_NOTIFY_HXX

struct pool;
class Istream;

struct istream_notify_handler {
    void (*eof)(void *ctx);
    void (*abort)(void *ctx);
    void (*close)(void *ctx);
};

Istream *
istream_notify_new(struct pool &pool, Istream &input,
                   const istream_notify_handler &handler, void *ctx);

#endif
