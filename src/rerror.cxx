/*
 * Convert GError to a HTTP response.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request.hxx"
#include "translate_client.hxx"
#include "http_client.hxx"
#include "ajp_client.hxx"
#include "memcached_client.hxx"
#include "cgi_quark.h"
#include "fcgi_quark.h"
#include "was_quark.h"
#include "widget-quark.h"
#include "http_error.h"
#include "http_response.h"
#include "http_server.hxx"
#include "http_quark.h"
#include "gerrno.h"
#include "pool.h"

#ifdef HAVE_LIBNFS
#include "nfs_client.h"
#include <nfsc/libnfs-raw-nfs.h>
#endif

void
response_dispatch_error(struct request *request, GError *error)
{
    if (error->domain == http_response_quark()) {
        response_dispatch_message(request, http_status_t(error->code),
                                  p_strdup(request->request->pool,
                                           error->message));
        return;
    }

    if (error->domain == widget_quark()) {
        switch ((enum widget_error)error->code) {
        case WIDGET_ERROR_UNSPECIFIED:
            break;

        case WIDGET_ERROR_EMPTY:
        case WIDGET_ERROR_WRONG_TYPE:
        case WIDGET_ERROR_UNSUPPORTED_ENCODING:
            response_dispatch_message(request, HTTP_STATUS_BAD_GATEWAY,
                                      "Malformed widget response");
            return;

        case WIDGET_ERROR_NO_SUCH_VIEW:
            response_dispatch_message(request, HTTP_STATUS_NOT_FOUND,
                                      "No such view");
            return;

        case WIDGET_ERROR_NOT_A_CONTAINER:
            response_dispatch_message(request, HTTP_STATUS_NOT_FOUND,
                                      error->message);
            return;

        case WIDGET_ERROR_FORBIDDEN:
            response_dispatch_message(request, HTTP_STATUS_FORBIDDEN,
                                      "Forbidden");
            return;
        }
    }

#ifdef HAVE_LIBNFS
    if (error->domain == nfs_client_quark()) {
        switch (error->code) {
        case NFS3ERR_NOENT:
        case NFS3ERR_NOTDIR:
            response_dispatch_message(request, HTTP_STATUS_NOT_FOUND,
                                      "The requested file does not exist.");
            return;
        }
    }
#endif

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
    else if (error->domain == errno_quark()) {
        struct http_response_handler_ref ref;
        http_response_handler_set(&ref, &response_handler, request);
        http_response_handler_invoke_errno(&ref, request->request->pool,
                                           error->code);
    } else if (error->domain == memcached_client_quark())
        response_dispatch_message(request, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  "Cache server failed");
    else
        response_dispatch_message(request, HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                  "Internal server error");
}
