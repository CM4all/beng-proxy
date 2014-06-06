/*
 * Handle cookies sent by the HTTP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_COOKIE_SERVER_HXX
#define BENG_PROXY_COOKIE_SERVER_HXX

struct pool;
struct strmap;

/**
 * Parse a Cookie request header and store all cookies in the
 * specified strmap.
 */
void
cookie_map_parse(struct strmap *cookies, const char *p, struct pool *pool);

#endif
