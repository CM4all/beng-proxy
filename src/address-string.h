/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ADDRESS_STRING_H
#define BENG_PROXY_ADDRESS_STRING_H

struct pool;
struct addrinfo;

struct address_envelope *
address_envelope_parse(struct pool *pool, const char *p, int default_port);

#endif
