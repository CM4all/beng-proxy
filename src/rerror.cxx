/*
 * Convert GError to a HTTP response.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request.hxx"
#include "bp_connection.hxx"
#include "bp_instance.hxx"
#include "translate_quark.hxx"
#include "http_client.hxx"
#include "ajp/ajp_client.hxx"
#include "memcached_client.hxx"
#include "cgi/cgi_quark.h"
#include "fcgi/fcgi_quark.h"
#include "was/was_quark.h"
#include "widget-quark.h"
#include "http_response.hxx"
#include "http_server.hxx"
#include "http_quark.h"
#include "http_domain.hxx"
#include "gerrno.h"
#include "pool.hxx"
#include "util/Error.hxx"

#include <daemon/log.h>

#ifdef HAVE_LIBNFS
#include "nfs_client.hxx"
#include <nfsc/libnfs-raw-nfs.h>
#endif

static void
response_dispatch_error(Request &request, GError *error,
                        http_status_t status, const char *message)
{
    if (request.connection->instance->config.verbose_response)
        message = p_strdup(request.request->pool, error->message);

    response_dispatch_message(request, status, message);
}

static void
response_dispatch_error(Request &request, Error &&error,
                        http_status_t status, const char *message)
{
    if (request.connection->instance->config.verbose_response)
        message = p_strdup(request.request->pool, error.GetMessage());

    response_dispatch_message(request, status, message);
}

void
response_dispatch_error(Request &request, GError *error)
{
    if (error->domain == http_response_quark()) {
        response_dispatch_message(request, http_status_t(error->code),
                                  p_strdup(request.request->pool,
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
                                      p_strdup(request.request->pool,
                                               error->message));
            return;

        case WIDGET_ERROR_FORBIDDEN:
            response_dispatch_error(request, error, HTTP_STATUS_FORBIDDEN,
                                    "Forbidden");
            return;
        }
    }

#ifdef HAVE_LIBNFS
    if (error->domain == nfs_client_quark()) {
        switch (error->code) {
        case NFS3ERR_NOENT:
        case NFS3ERR_NOTDIR:
            response_dispatch_error(request, error, HTTP_STATUS_NOT_FOUND,
                                    "The requested file does not exist.");
            return;
        }
    }
#endif

    if (error->domain == translate_quark())
        response_dispatch_error(request, error, HTTP_STATUS_BAD_GATEWAY,
                                "Translation server failed");
    else if (error->domain == http_client_quark() ||
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
response_dispatch_error(Request &request, Error &&error)
{
    if (error.IsDomain(http_response_domain)) {
        response_dispatch_message(request, http_status_t(error.GetCode()),
                                  p_strdup(request.request->pool,
                                           error.GetMessage()));
        return;
    } else if (error.IsDomain(errno_domain)) {
        switch (error.GetCode()) {
        case ENOENT:
        case ENOTDIR:
            response_dispatch_error(request, std::move(error),
                                    HTTP_STATUS_NOT_FOUND,
                                    "The requested file does not exist.");
            break;

        default:
            response_dispatch_error(request, std::move(error),
                                    HTTP_STATUS_INTERNAL_SERVER_ERROR,
                                    "Internal server error");
        }
    } else
        response_dispatch_error(request, std::move(error),
                                HTTP_STATUS_BAD_GATEWAY,
                                "Internal server error");
}

void
response_dispatch_log(Request &request, http_status_t status,
                      const char *msg, const char *log_msg)
{
    daemon_log(2, "%s\n", log_msg);

    if (request.connection->instance->config.verbose_response)
        msg = p_strdup(request.request->pool, log_msg);

    response_dispatch_message(request, status, msg);
}

void
response_dispatch_log(Request &request, http_status_t status,
                      const char *log_msg)
{
    response_dispatch_log(request, status,
                          http_status_to_string(status), log_msg);
}
