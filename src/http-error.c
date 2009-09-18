/*
 * Delivering plain-text error messages.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http-error.h"
#include "http-response.h"

#include <errno.h>

void
http_response_handler_invoke_errno(struct http_response_handler_ref *handler,
                                   pool_t pool, int error)
{
    switch (error) {
    case ENOENT:
        http_response_handler_invoke_message(handler, pool,
                                             HTTP_STATUS_NOT_FOUND,
                                             "The requested file does not exist.");
        break;

    default:
        http_response_handler_invoke_abort(handler);
    }
}
