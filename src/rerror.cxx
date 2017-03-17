/*
 * Convert GError to a HTTP response.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request.hxx"
#include "bp_instance.hxx"
#include "http_client.hxx"
#include "nfs_client.hxx"
#include "ajp/ajp_client.hxx"
#include "memcached/memcached_client.hxx"
#include "cgi/cgi_quark.h"
#include "fcgi/Quark.hxx"
#include "was/was_quark.h"
#include "widget-quark.h"
#include "http_response.hxx"
#include "http_server/http_server.hxx"
#include "http_server/Request.hxx"
#include "http_quark.h"
#include "HttpMessageResponse.hxx"
#include "gerrno.h"
#include "pool.hxx"
#include "util/Exception.hxx"

#include <daemon/log.h>

#include <nfsc/libnfs-raw-nfs.h>

static void
response_dispatch_error(Request &request, GError *error,
                        http_status_t status, const char *message)
{
    if (request.instance.config.verbose_response)
        message = p_strdup(&request.pool, error->message);

    response_dispatch_message(request, status, message);
}

void
response_dispatch_error(Request &request, GError *error)
{
    if (error->domain == http_response_quark()) {
        response_dispatch_message(request, http_status_t(error->code),
                                  p_strdup(&request.pool,
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
            response_dispatch_error(request, error, HTTP_STATUS_BAD_GATEWAY,
                                    "Malformed widget response");
            return;

        case WIDGET_ERROR_NO_SUCH_VIEW:
            response_dispatch_error(request, error, HTTP_STATUS_NOT_FOUND,
                                    "No such view");
            return;

        case WIDGET_ERROR_NOT_A_CONTAINER:
            response_dispatch_message(request, HTTP_STATUS_NOT_FOUND,
                                      p_strdup(&request.pool,
                                               error->message));
            return;

        case WIDGET_ERROR_FORBIDDEN:
            response_dispatch_error(request, error, HTTP_STATUS_FORBIDDEN,
                                    "Forbidden");
            return;
        }
    }

    if (error->domain == nfs_client_quark()) {
        switch (error->code) {
        case NFS3ERR_NOENT:
        case NFS3ERR_NOTDIR:
            response_dispatch_error(request, error, HTTP_STATUS_NOT_FOUND,
                                    "The requested file does not exist.");
            return;
        }
    }

    if (error->domain == http_client_quark() ||
             error->domain == ajp_client_quark())
        response_dispatch_error(request, error, HTTP_STATUS_BAD_GATEWAY,
                                "Upstream server failed");
    else if (error->domain == cgi_quark() ||
             error->domain == fcgi_quark() ||
             error->domain == was_quark())
        response_dispatch_error(request, error, HTTP_STATUS_BAD_GATEWAY,
                                "Script failed");
    else if (error->domain == errno_quark()) {
        switch (error->code) {
        case ENOENT:
        case ENOTDIR:
            response_dispatch_error(request, error, HTTP_STATUS_NOT_FOUND,
                                    "The requested file does not exist.");
            break;

        default:
            response_dispatch_error(request, error,
                                    HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                    "Internal server error");
        }
    } else if (error->domain == memcached_client_quark())
        response_dispatch_error(request, error,
                                HTTP_STATUS_BAD_GATEWAY,
                                "Cache server failed");
    else
        response_dispatch_error(request, error,
                                HTTP_STATUS_BAD_GATEWAY,
                                "Internal server error");
}

void
response_dispatch_log(Request &request, http_status_t status,
                      const char *msg, const char *log_msg)
{
    daemon_log(2, "error on '%s': %s\n", request.request.uri, log_msg);

    if (request.instance.config.verbose_response)
        msg = p_strdup(&request.pool, log_msg);

    response_dispatch_message(request, status, msg);
}

void
response_dispatch_log(Request &request, http_status_t status,
                      const char *log_msg)
{
    response_dispatch_log(request, status,
                          http_status_to_string(status), log_msg);
}

void
response_dispatch_log(Request &request, http_status_t status,
                      const char *msg,
                      std::exception_ptr ep)
{
    auto log_msg = GetFullMessage(ep);
    daemon_log(2, "error on '%s': %s\n", request.request.uri, log_msg.c_str());

    try {
        FindRetrowNested<HttpMessageResponse>(ep);
    } catch (const HttpMessageResponse &e) {
        status = e.GetStatus();
        msg = p_strdup(&request.pool, e.what());
    }

    if (request.instance.config.verbose_response)
        msg = p_strdup(&request.pool, log_msg.c_str());

    response_dispatch_message(request, status, msg);
}
