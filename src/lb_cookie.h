/*
 * Node selection by cookie.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_COOKIE_H
#define BENG_PROXY_LB_COOKIE_H

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

#endif
