/*
 * Handle cookies sent by the HTTP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_COOKIE_SERVER_H
#define __BENG_COOKIE_SERVER_H

struct pool;
struct strmap;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parse a Cookie request header and store all cookies in the
 * specified strmap.
 */
void
cookie_map_parse(struct strmap *cookies, const char *p, struct pool *pool);

#ifdef __cplusplus
}
#endif

#endif
