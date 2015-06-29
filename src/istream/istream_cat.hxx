/*
 * Concatenate several istreams.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_CAT_HXX
#define BENG_PROXY_ISTREAM_CAT_HXX

struct pool;
struct istream;

struct istream *
istream_cat_new(struct pool *pool, ...);

#endif
