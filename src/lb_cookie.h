/*
 * Node selection by cookie.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_COOKIE_H
#define BENG_PROXY_LB_COOKIE_H

#include <assert.h>

struct strmap;

/**
 * Extract a node cookie from the request headers.
 */
unsigned
lb_cookie_get(const struct strmap *request_headers);

/**
 * Select a random worker.
 *
 * @param n the number of nodes in the cluster
 * @return a random number between 1 and n (both including)
 */
unsigned
lb_cookie_generate(unsigned n);

/**
 * Calculate the next worker number.
 */
static inline unsigned
lb_cookie_next(unsigned n, unsigned i)
{
    assert(n >= 2);
    assert(i >= 1 && i <= n);

    return (i % n) + 1;
}

#endif
