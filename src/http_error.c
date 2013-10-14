/*
 * Delivering plain-text error messages.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_error.h"
#include "http_response.h"

#include <errno.h>
#include <string.h>

void
http_response_handler_invoke_errno(struct http_response_handler_ref *handler,
                                   struct pool *pool, int error)
{
    switch (error) {
    case ENOENT:
    case ENOTDIR:
        http_response_handler_invoke_message(handler, pool,
                                             HTTP_STATUS_NOT_FOUND,
                                             "The requested file does not exist.");
        break;

    default:
        http_response_handler_invoke_abort(handler,
                                           g_error_new_literal(http_quark(),
                                                               0, strerror(error)));
    }
}
