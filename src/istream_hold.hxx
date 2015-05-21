/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_HOLD_HXX
#define BENG_PROXY_ISTREAM_HOLD_HXX

struct pool;
struct istream;

/**
 * An istream facade which waits for the istream handler to appear.
 * Until then, it blocks all read requests from the inner stream.
 *
 * This class is required because all other istreams require a handler
 * to be installed.  In the case of HTTP proxying, the request body
 * istream has no handler until the connection to the other HTTP
 * server is open.  Meanwhile, istream_hold blocks all read requests
 * from the client's request body.
 */
struct istream *
istream_hold_new(struct pool *pool, struct istream *input);

#endif
