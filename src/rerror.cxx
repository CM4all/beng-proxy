/*
 * Convert GError to a HTTP response.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "request.hxx"
#include "bp_instance.hxx"
#include "http_client.hxx"
#include "nfs/Quark.hxx"
#include "ajp/ajp_client.hxx"
#include "memcached/memcached_client.hxx"
#include "cgi/cgi_quark.h"
#include "fcgi/Quark.hxx"
#include "was/was_quark.h"
#include "widget/Error.hxx"
#include "http_response.hxx"
#include "http_server/http_server.hxx"
#include "http_server/Request.hxx"
#include "http_quark.h"
#include "http/MessageHttpResponse.hxx"
#include "HttpMessageResponse.hxx"
#include "gerrno.h"
#include "pool.hxx"
#include "util/Exception.hxx"

#include <daemon/log.h>

#include <nfsc/libnfs-raw-nfs.h>

static MessageHttpResponse
Dup(struct pool &pool, http_status_t status, const char *msg)
{
    return {status, p_strdup(&pool, msg)};
}

gcc_pure
static MessageHttpResponse
ToResponse(struct pool &pool, GError &error)
{
    if (error.domain == http_response_quark())
        return Dup(pool, http_status_t(error.code), error.message);

    if (error.domain == widget_quark()) {
        switch (WidgetErrorCode(error.code)) {
        case WidgetErrorCode::UNSPECIFIED:
            break;

        case WidgetErrorCode::WRONG_TYPE:
        case WidgetErrorCode::UNSUPPORTED_ENCODING:
            return {HTTP_STATUS_BAD_GATEWAY, "Malformed widget response"};

        case WidgetErrorCode::NO_SUCH_VIEW:
            return {HTTP_STATUS_NOT_FOUND, "No such view"};

        case WidgetErrorCode::NOT_A_CONTAINER:
            return Dup(pool, HTTP_STATUS_NOT_FOUND, error.message);

        case WidgetErrorCode::FORBIDDEN:
            return {HTTP_STATUS_FORBIDDEN, "Forbidden"};
        }
    }

    if (error.domain == nfs_client_quark()) {
        switch (error.code) {
        case NFS3ERR_NOENT:
        case NFS3ERR_NOTDIR:
            return {HTTP_STATUS_NOT_FOUND,
                    "The requested file does not exist."};
        }
    }

    if (error.domain == http_client_quark() ||
             error.domain == ajp_client_quark())
        return {HTTP_STATUS_BAD_GATEWAY, "Upstream server failed"};
    else if (error.domain == cgi_quark() ||
             error.domain == fcgi_quark() ||
             error.domain == was_quark())
        return {HTTP_STATUS_BAD_GATEWAY, "Script failed"};
    else if (error.domain == errno_quark()) {
        switch (error.code) {
        case ENOENT:
        case ENOTDIR:
            return {HTTP_STATUS_NOT_FOUND,
                    "The requested file does not exist."};
            break;

        default:
            return {HTTP_STATUS_INTERNAL_SERVER_ERROR,
                    "Internal server error"};
        }
    } else if (error.domain == memcached_client_quark())
        return {HTTP_STATUS_BAD_GATEWAY, "Cache server failed"};
    else
        return {HTTP_STATUS_INTERNAL_SERVER_ERROR, "Internal server error"};
}

gcc_pure
static MessageHttpResponse
ToResponse(struct pool &pool, std::exception_ptr ep)
{
    try {
        FindRetrowNested<HttpMessageResponse>(ep);
    } catch (const HttpMessageResponse &e) {
        return Dup(pool, e.GetStatus(), e.what());
    }

    try {
        FindRetrowNested<WidgetError>(ep);
    } catch (const WidgetError &e) {
        switch (e.GetCode()) {
        case WidgetErrorCode::UNSPECIFIED:
            break;

        case WidgetErrorCode::WRONG_TYPE:
        case WidgetErrorCode::UNSUPPORTED_ENCODING:
            return {HTTP_STATUS_BAD_GATEWAY, "Malformed widget response"};

        case WidgetErrorCode::NO_SUCH_VIEW:
            return {HTTP_STATUS_NOT_FOUND, "No such view"};

        case WidgetErrorCode::NOT_A_CONTAINER:
            return Dup(pool, HTTP_STATUS_NOT_FOUND, e.what());

        case WidgetErrorCode::FORBIDDEN:
            return {HTTP_STATUS_FORBIDDEN, "Forbidden"};
        }
    }

    return {HTTP_STATUS_INTERNAL_SERVER_ERROR, "Internal server error"};
}

void
response_dispatch_error(Request &request, GError *error)
{
    auto response = ToResponse(request.pool, *error);
    if (request.instance.config.verbose_response)
        response.message = p_strdup(&request.pool, error->message);

    response_dispatch_message(request, response.status, response.message);
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
response_dispatch_log(Request &request, std::exception_ptr ep)
{
    auto log_msg = GetFullMessage(ep);
    daemon_log(2, "error on '%s': %s\n", request.request.uri, log_msg.c_str());

    auto response = ToResponse(request.pool, ep);
    if (request.instance.config.verbose_response)
        response.message = p_strdup(&request.pool, log_msg.c_str());

    response_dispatch_message(request, response.status, response.message);
}
