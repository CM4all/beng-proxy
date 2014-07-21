/*
 * An istream facade which invokes a callback when the istream is
 * finished / closed.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_NOTIFY_H
#define BENG_PROXY_ISTREAM_NOTIFY_H

struct pool;
struct istream;

struct istream_notify_handler {
    void (*eof)(void *ctx);
    void (*abort)(void *ctx);
    void (*close)(void *ctx);
};

#ifdef __cplusplus
extern "C" {
#endif

struct istream *
istream_notify_new(struct pool *pool, struct istream *input,
                   const struct istream_notify_handler *handler, void *ctx);

#ifdef __cplusplus
}
#endif

#endif
