/*
 * This istream filter catches fatal errors and attempts to ignore
 * them.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_CATCH_H
#define BENG_PROXY_ISTREAM_CATCH_H

#include <glib.h>

struct pool;
struct istream;

#ifdef __cplusplus
extern "C" {
#endif

struct istream *
istream_catch_new(struct pool *pool, struct istream *input,
                  GError *(*callback)(GError *error, void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif

#endif
