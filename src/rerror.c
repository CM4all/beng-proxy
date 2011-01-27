/*
 * Convert GError to a HTTP response.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request.h"

void
response_dispatch_error(struct request *request, GError *error)
{
    (void)error;

    response_dispatch_message(request, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                              "Internal server error");
}
