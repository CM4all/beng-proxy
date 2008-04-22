/*
 * Handle cookies sent by the HTTP client.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_COOKIE_SERVER_H
#define __BENG_COOKIE_SERVER_H

#include "pool.h"

struct strmap;

/**
 * Parse a Cookie request header and store all cookies in the
 * specified strmap.
 */
void
cookie_map_parse(struct strmap *cookies, const char *p, pool_t pool);

#endif
