/*
 * Convert GError to a HTTP response.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request.h"
#include "translate.h"
#include "http-client.h"
#include "ajp-client.h"
#include "memcached-client.h"
#include "cgi.h"
#include "fcgi-quark.h"
#include "was-quark.h"

void
response_dispatch_error(struct request *request, GError *error)
{
    (void)error;

    if (error->domain == translate_quark())
        response_dispatch_message(request, HTTP_STATUS_BAD_GATEWAY,
                                  "Translation server failed");
    else if (error->domain == http_client_quark() ||
             error->domain == ajp_client_quark())
        response_dispatch_message(request, HTTP_STATUS_BAD_GATEWAY,
                                  "Upstream server failed");
    else if (error->domain == cgi_quark() ||
             error->domain == fcgi_quark() ||
             error->domain == was_quark())
        response_dispatch_message(request, HTTP_STATUS_BAD_GATEWAY,
                                  "Script failed");
    else if (error->domain == g_file_error_quark())
        response_dispatch_message(request, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  "I/O error");
    else if (error->domain == memcached_client_quark())
        response_dispatch_message(request, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  "Cache server failed");
    else
        response_dispatch_message(request, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  "Internal server error");
}
