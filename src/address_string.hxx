/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ADDRESS_STRING_HXX
#define BENG_PROXY_ADDRESS_STRING_HXX

#include "glibfwd.hxx"

struct pool;
struct addrinfo;

/**
 * Parse a numeric socket address for use with stream sockets
 * (e.g. TCP).
 *
 * @param passive if true, then NULL or "*" will return the wildcard
 * address
 */
struct address_envelope *
address_envelope_parse(struct pool *pool, const char *p, int default_port,
                       bool passive, GError **error_r);

#endif
