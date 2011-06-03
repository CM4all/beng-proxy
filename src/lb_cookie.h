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
 * Extract a node cookie from the request headers.
 */
unsigned
lb_cookie_generate(unsigned n);

#endif
