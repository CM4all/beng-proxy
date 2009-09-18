/*
 * Delivering plain-text error messages.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_ERROR_H
#define BENG_PROXY_HTTP_ERROR_H

#include "pool.h"
#include "http.h"

struct http_response_handler_ref;

/**
 * Sends a response according to the specified errno value.
 */
void
http_response_handler_invoke_errno(struct http_response_handler_ref *handler,
                                   pool_t pool, int error);

#endif
