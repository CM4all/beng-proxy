/*
 * Generate custom HTTP responses.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CUSTOM_RESPONSE_H
#define BENG_PROXY_CUSTOM_RESPONSE_H

struct request;

void
method_not_allowed(struct request *request2, const char *allow);

#endif
