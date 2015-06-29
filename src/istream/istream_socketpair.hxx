/*
 * This istream implementation creates a socket pair with
 * socketpair().  It provides one side as istream/handler pair, and
 * returns the other socket descriptor.  You may use this to integrate
 * code into the istream framework which works only with a socket
 * descriptor.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_SOCKETPAIR_HXX
#define BENG_PROXY_ISTREAM_SOCKETPAIR_HXX

struct pool;
struct istream;

struct istream *
istream_socketpair_new(struct pool *pool, struct istream *input, int *fd_r);

#endif
