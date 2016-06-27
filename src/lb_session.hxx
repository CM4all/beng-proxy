/*
 * Session handling.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_SESSION_H
#define BENG_PROXY_LB_SESSION_H

class StringMap;

/**
 * Extract a session identifier from the request headers.
 */
unsigned
lb_session_get(const StringMap &request_headers,
               const char *cookie_name);

#endif
