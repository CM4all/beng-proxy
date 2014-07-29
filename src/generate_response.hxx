/*
 * Generate custom HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CUSTOM_RESPONSE_HXX
#define BENG_PROXY_CUSTOM_RESPONSE_HXX

struct request;

void
method_not_allowed(struct request &request, const char *allow);

#endif
